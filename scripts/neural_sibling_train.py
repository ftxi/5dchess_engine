#!/usr/bin/env python3
"""Fit and export value networks from judged sibling positions in label panels."""
import argparse
import copy
from collections import Counter
from dataclasses import dataclass
import hashlib
from itertools import combinations
import json
from pathlib import Path

import numpy as np
from scipy.optimize import minimize
from scipy.special import expit
import torch
import torch.nn.functional as F

import autoplay
from neural_dataset import digest, extract_features, RAY_SCHEMA, SCHEMA, save
from neural_train import export_model, Network, normalization


@dataclass
class Root:
    state: str
    family: str
    black: bool
    features: list
    values: list
    # Indices ordered by the parent mover's preference.
    pairs: list
    terminal_pairs: list


def family_groups(rows):
    parents = {r['family']: r['family'] for r in rows}
    def find(key):
        while parents[key] != key:
            parents[key] = parents[parents[key]]
            key = parents[key]
        return key

    seen = {}
    for row in rows:
        for state in [row['state_hash'], *(c['state_hash'] for c in row['candidates'])]:
            family = seen.setdefault(state, row['family'])
            a, b = sorted((find(family), find(row['family'])))
            parents[b] = a
    return {family: find(family) for family in parents}


def read_root(row, rules, family):
    black = row['black_to_move']
    root = Root(row['state_hash'], family, black, [], [], [], [])
    candidates = {}
    for c in row['candidates']:
        prefix, size, parity, fen_tag, fen = c['position'].split(' ', 4)
        if (prefix, fen_tag) != ('size', 'fen'):
            raise ValueError('invalid successor snapshot')
        game = rules.game.from_pgn(f'[Size "{size}"]\n[Timeline "{parity}"]\n' + fen.replace('] [', ']\n['))
        successor_black = bool(game.get_current_present()[1])
        h = c['state_hash']
        if (str(game.get_current_hash()) != h or h in candidates
                or successor_black != c['successor_black'] or successor_black == black):
            raise ValueError(f'inconsistent successor identity or perspective at {h}')
        features = extract_features(game, SCHEMA)
        if features[:len(c['features'])] != c['features']:
            raise ValueError(f'stored feature mismatch at {h}')
        judges = c['judge_5000']
        expected = [(engine, seed) for engine in ('solid', 'solid-amaf1') for seed in (17, 73)]
        if [(j['engine'], j['seed']) for j in judges] != expected:
            raise ValueError('unexpected judge configuration')
        terminal = bool(judges[0].get('terminal'))
        white = np.array([j['white'] for j in judges])
        parent_values = white * (-1 if black else 1)
        if (not np.isfinite(white).all() or (abs(white) > 1).any()
                or any('error' in j or bool(j.get('terminal')) != terminal
                       or (not terminal and j['iterations'] != 5000) for j in judges)
                or not np.array_equal(parent_values, [j['mover'] for j in judges])):
            raise ValueError(f'inconsistent judges at {h}')
        if terminal:
            exact_white = {rules.match_status_t.WHITE_WINS: 1,
                           rules.match_status_t.BLACK_WINS: -1,
                           rules.match_status_t.STALEMATE: 0}.get(game.get_match_status())
            if exact_white is None or not (white == exact_white).all():
                raise ValueError(f'terminal label disagrees with rules at {h}')
        index = None
        if not terminal:
            index = len(root.features)
            root.features.append(features)
            root.values.append(float(white.mean() * (-1 if successor_black else 1)))
        candidates[h] = index, parent_values

    saved = {(p['first'], p['second']): p for p in row['pairs_5000']}
    if set(saved) != set(combinations(candidates, 2)) or len(saved) != len(row['pairs_5000']):
        raise ValueError('incomplete sibling pairs')
    any_stable = False
    for (a, b), p in saved.items():
        i, va = candidates[a]
        j, vb = candidates[b]
        gaps = va - vb
        label = 'tie' if (gaps == 0).all() else 'unresolved'
        for sign, name in ((1, 'first'), (-1, 'second')):
            directed = sign * gaps
            if (directed > 0).all() and min(directed[:2].mean(), directed[2:].mean()) >= .05:
                label = name
        if label != p['label'] or not np.allclose(gaps, p['gaps'], rtol=0, atol=1e-12):
            raise ValueError('saved pair disagrees with judge scores')
        if label in ('first', 'second'):
            any_stable = True
            if i is not None and j is not None:
                root.pairs.append((i, j) if label == 'first' else (j, i))
            else:
                # The parent prefers the smaller successor-mover value.
                pair = ((i, -va[0]), (j, -vb[0]))
                root.terminal_pairs.append(pair if label == 'first' else pair[::-1])
    return root, any_stable


class Panel:
    def __init__(self, roots):
        self.roots = roots
        self.raw = np.array([x for r in roots for x in r.features], dtype=np.float32)
        self.values = torch.tensor([v for r in roots for v in r.values])
        self.value_root = np.repeat(np.arange(len(roots)), [len(r.values) for r in roots])
        self.pair_root = np.repeat(np.arange(len(roots)), [len(r.pairs) for r in roots])
        self.groups = np.array([r.family for r in roots])
        offset = 0
        pairs = []
        for r in roots:
            pairs.extend((offset+i, offset+j) for i, j in r.pairs)
            offset += len(r.features)
        self.preferred, self.other = np.asarray(pairs, dtype=int).T

    def weights(self, indices, root_indices):
        counts = Counter(indices)
        groups = Counter(self.groups[i] for i in counts if i in root_indices)
        w = np.array([1/(counts[i]*groups[self.groups[i]]) if i in root_indices else 0 for i in indices])
        if not w.sum():
            raise ValueError('empty training or validation term')
        return w / w.sum()

    def split(self, indices):
        selected = set(indices)
        return (torch.from_numpy(self.weights(self.value_root, selected)).float(),
                torch.from_numpy(self.weights(self.pair_root, selected)).float())

    def linear_scores(self, columns, folds):
        # A lower successor-mover value is better for the player choosing the action.
        d = (self.raw[self.other] - self.raw[self.preferred])[:, columns].astype(np.float64)
        logits = np.zeros(len(d))
        for k in range(5):
            w = self.weights(self.pair_root, set(np.flatnonzero(folds != k)))
            train = w > 0
            test = folds[self.pair_root] == k
            rms = np.sqrt(w[train] @ (d[train]**2))
            keep = rms > 1e-8
            x = d[:, keep] / rms[keep]
            def objective(beta):
                margin = x[train] @ beta
                return (w[train] @ np.logaddexp(0, -margin) + .05*(beta @ beta),
                        x[train].T @ (-w[train]*expit(-margin)) + .1*beta)
            fit = minimize(objective, np.zeros(sum(keep)), jac=True, method='L-BFGS-B', tol=1e-9)
            if not fit.success:
                raise RuntimeError(fit.message)
            logits[test] = x[test] @ fit.x
        def score(indices):
            w = self.weights(self.pair_root, set(indices))
            return dict(accuracy=float(w @ np.where(abs(logits)<1e-10, .5, logits>0)),
                        pair_loss=float(w @ np.logaddexp(0, -logits)))
        return dict(cross_validation=score(np.arange(len(folds))), validation=score(np.flatnonzero(folds==0)))


def metrics(z, panel, value_weights, pair_weights):
    # tanh(z) gives the successor value; 2z is its log-odds.
    margin = 2*(z[panel.other] - z[panel.preferred])
    return dict(pair_loss=float(pair_weights @ F.softplus(-margin)),
                accuracy=float(pair_weights @ ((margin > 0).float() + .5*(margin == 0).float())),
                value_mse=float(value_weights @ (z.tanh()-panel.values).square()))


def fit_network(panel, x, train, weights, validation, seed):
    torch.manual_seed(seed)
    model = Network(x.shape[1])
    optimizer = torch.optim.AdamW(model.parameters(), lr=.001, weight_decay=.0001)
    rng = np.random.default_rng(seed)
    best_loss = float('inf')
    history = []
    for epoch in range(1, 41):
        order = rng.permutation(train)
        for start in range(0, len(order), 32):
            batch = order[start:start+32]
            vi = np.flatnonzero(np.isin(panel.value_root, batch))
            pi = np.flatnonzero(np.isin(panel.pair_root, batch))
            z = model(x[vi])
            value = F.binary_cross_entropy_with_logits(2*z, (panel.values[vi]+1)/2, reduction='none')
            loss = weights[0][vi] @ value
            if len(pi):
                margin = 2*(z[np.searchsorted(vi,panel.other[pi])] - z[np.searchsorted(vi,panel.preferred[pi])])
                loss = loss + weights[1][pi] @ F.softplus(-margin)
            optimizer.zero_grad(set_to_none=True)
            (.5 * len(order)/len(batch) * loss).backward()
            optimizer.step()
        with torch.no_grad():
            record = metrics(model(x), panel, *validation)
        history.append(record)
        if not all(np.isfinite(list(record.values()))):
            raise ValueError('nonfinite validation metrics')
        if record['pair_loss'] < best_loss:
            best_loss = record['pair_loss']
            best_epoch = epoch
            state = copy.deepcopy(model.state_dict())
        elif epoch-best_epoch >= 5:
            break
    model.load_state_dict(state)
    return model, dict(seed=seed, epoch=best_epoch, validation=history[best_epoch-1], history=history)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--labels', type=Path, nargs='+', required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rules = autoplay.load_rules(args.build)
    torch.set_num_threads(1)
    torch.use_deterministic_algorithms(True)
    rows = [r for path in args.labels for r in json.loads(path.read_text())]
    if len({r['state_hash'] for r in rows}) != len(rows):
        raise ValueError('duplicate roots in label panels')
    groups = family_groups(rows)
    roots, terminal_only, stable = [], [], set()
    for row in rows:
        root, any_stable = read_root(row, rules, groups[row['family']])
        if any_stable:
            stable.add(root.family)
        if root.values:
            roots.append(root)
        else:
            terminal_only.append(root)
    panel = Panel(roots)
    def group_key(group):
        return hashlib.sha256(f'20261123:{group}'.encode()).digest()
    group_fold = {g: i%5 for i,g in enumerate(sorted(stable,key=group_key))}
    for g in set(groups.values())-stable:
        group_fold[g] = int.from_bytes(group_key(g)[:4], 'big')%5
    folds = np.array([group_fold[g] for g in panel.groups])
    train = np.flatnonzero(folds!=0)
    validation = panel.split(np.flatnonzero(folds==0))
    weights = panel.split(train)
    args.output.mkdir(parents=True, exist_ok=False)
    save(args.output/'splits.json', {f:dict(group=g,fold=group_fold[g]) for f,g in groups.items()})
    report = dict(panels={str(p):digest(p) for p in args.labels}, binding=digest(rules.__file__),
                  script=digest(__file__), torch=torch.__version__, roots=len(rows),
                  value_rows=len(panel.raw), stable_nonterminal_pairs=len(panel.preferred),
                  ranking_groups=len(set(panel.groups[panel.pair_root])), validation_pairs=int((folds[panel.pair_root]==0).sum()),
                  validation_value_rows=int((folds[panel.value_root]==0).sum()),
                  loss='Equal parts successor soft-target BCE and paired preference log loss',
                  normalization='Training split only, equal group and root weights',
                  optimizer=dict(name='AdamW', learning_rate=.001, weight_decay=.0001,
                                 root_batch=32, patience=5, max_epochs=40),
                  validation_fold=0, selection_metric='validation pair log loss',
                  linear={}, models={})
    variants = [('80',80,[]), ('87-no-protection',87,[80,81,82]), ('87',87,[])]
    for name,width,mask in variants:
        cols = [i for i in range(width) if i not in mask]
        report['linear'][name] = panel.linear_scores(cols,folds)
    print('Linear controls:',report['linear'],flush=True)
    best = float('inf')
    for name,width,mask in variants:
        selected = weights[0].numpy()>0
        raw = panel.raw[:,:width]
        mean, inv, _ = normalization(raw[selected],weights[0][selected].numpy())
        inv[mask] = 0
        x = torch.from_numpy((raw-mean)*inv)
        for seed in (17,42,2026):
            model, record = fit_network(panel,x,train,weights,validation,seed)
            key = f'{name}-seed{seed}'
            path = args.output/(key+'.nn')
            export_model(path,model,mean,inv,RAY_SCHEMA if width==80 else SCHEMA)
            native = rules.neural_model.load(str(path))
            actual = np.array([native.evaluate_features(row.tolist()) for row in raw])
            with torch.no_grad():
                expected = model(x).tanh().numpy()
            error = float(np.max(np.abs(actual-expected)))
            if not error <= 1e-5:
                raise ValueError('native parity failure')
            report['models'][key] = dict(**record, native_error=error, sha256=digest(path))
            if record['validation']['pair_loss'] < best:
                best = record['validation']['pair_loss']
                report['selected'] = key
    # Score terminal-involving comparisons separately with the engine's exact
    # terminal values and the selected network's nonterminal predictions.
    source = args.output/(report['selected']+'.nn')
    best_model = rules.neural_model.load(str(source))
    best_width = int(source.read_text().splitlines()[2].split()[0])
    terminal_results = []
    for root in [*roots, *terminal_only]:
        if group_fold[root.family] != 0:
            continue
        for pair in root.terminal_pairs:
            preferred, other = [exact if i is None else
                                best_model.evaluate_features(root.features[i][:best_width])
                                for i, exact in pair]
            terminal_results.append(float(preferred < other) + .5*(preferred == other))
    report['terminal_diagnostic'] = {
        'validation_stable_pairs': len(terminal_results),
        'unweighted_engine_order_accuracy': float(np.mean(terminal_results)) if terminal_results else None}

    (args.output/'model.nn').write_bytes(source.read_bytes())
    save(args.output/'training.json',report)
    print('Selected',report['selected'],best)


if __name__ == '__main__':
    main()

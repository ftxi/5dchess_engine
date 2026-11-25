#!/usr/bin/env python3
"""Validate online main lines in isolated processes and freeze family-level splits.

Only submitted states with a played continuation are sampled. Forked game trees
are excluded rather than attaching a played result to an unplayed variation.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import math
from pathlib import Path
import random
import re
import subprocess
import sys

import autoplay

ROOT = Path(__file__).resolve().parents[1]
BASE_SCHEMA = 'solid70-v2-mover'
RAY_SCHEMA = 'solid80-rays-v3-mover'
PROTECTION_SCHEMA = 'solid83-protection-v4-mover'
LOCAL_CHECK_SCHEMA = 'solid85-local-checks-v5-mover'
SCHEMA = 'solid87-capture-gain-v6-mover'
INPUT_COUNTS = {BASE_SCHEMA: 70, RAY_SCHEMA: 80, PROTECTION_SCHEMA: 83, LOCAL_CHECK_SCHEMA: 85, SCHEMA: 87}
FEATURE_GETTERS = {
    BASE_SCHEMA: 'get_neural_features',
    RAY_SCHEMA: 'get_neural_ray_features',
    PROTECTION_SCHEMA: 'get_neural_protection_features',
    LOCAL_CHECK_SCHEMA: 'get_neural_local_check_features',
    SCHEMA: 'get_neural_capture_gain_features',
}


def extract_features(game, schema):
    features = getattr(game, FEATURE_GETTERS[schema])()
    if len(features) != INPUT_COUNTS[schema] or not all(map(math.isfinite, features)):
        raise ValueError('nonfinite or wrong-size input')
    return list(features)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def collect(source, build, limit, seed, schema=SCHEMA):
    rules = autoplay.load_rules(build)
    game = rules.game.from_pgn(source.read_text())
    if game.has_illegal_history():
        raise ValueError('illegal history')
    metadata = dict(game.metadata)
    result = game.get_header('Result')
    if result not in ('1-0', '0-1', '1/2-1/2'):
        raise ValueError('missing or unsupported explicit result')
    # The loader validates all actions and royals. It lands at a final endpoint;
    # rewind that actual played path, rejecting any alternative continuations.
    history = []
    while True:
        children = game.get_child_actions()
        if len(children) > 1:
            raise ValueError('branching variation: played outcome is ambiguous')
        mandatory, optional, unplayable = game.get_current_timeline_status()
        history.append({'state_hash': str(game.get_current_hash()),
            'black_to_move': bool(game.get_current_present()[1]),
            'width': len(mandatory) + len(optional) + len(unplayable)})
        if not game.has_parent():
            break
        game.visit_parent()
    history.reverse()
    total = len(history) - 1
    if total < 9:
        raise ValueError('fewer than nine complete actions')
    # Skip generic openings (first eight actions), and the last endpoint. Every
    # selected state has a validated recorded action, hence is nonterminal.
    strata = defaultdict(list)
    for index in range(8, total):
        row = history[index]
        strata[(min(4, 5 * index // total), min(row['width'], 5),
                row['black_to_move'])].append(index)
    rng = random.Random(f'{seed}:{digest(source)}')
    groups = list(strata.values())
    rng.shuffle(groups)
    for group in groups:
        rng.shuffle(group)
    selected = []
    while groups and len(selected) < limit:
        remaining = []
        for group in groups:
            if len(selected) < limit:
                selected.append(group.pop())
            if group:
                remaining.append(group)
        groups = remaining
    selected = set(selected)
    samples = []
    white = {'1-0': 1, '0-1': -1, '1/2-1/2': 0}[result]
    for index in range(total):
        if index in selected:
            features = extract_features(game, schema)
            row = dict(history[index], action_index=index, total_actions=total,
                features=features, result=result,
                y_mover=-white if history[index]['black_to_move'] else white)
            samples.append(row)
        if index + 1 < total:
            children = game.get_child_actions()
            if len(children) != 1 or not game.visit_child(children[0].act):
                raise ValueError('cannot replay validated main line')
    return {'source': str(source.relative_to(ROOT)), 'source_sha256': digest(source),
        'result': result, 'metadata': metadata, 'total_actions': total,
        'history': history, 'samples': samples}


def isolated(source, args):
    try:
        proc = subprocess.run([sys.executable, str(Path(__file__).resolve()), '_collect',
            '--source', str(source), '--build', str(args.build),
            '--samples', str(args.samples), '--seed', str(args.seed), '--schema', args.schema],
            capture_output=True, text=True, timeout=args.timeout)
        if proc.returncode:
            return {'source': str(source.relative_to(ROOT)), 'exit_code': proc.returncode,
                'error': proc.stderr[-3000:] or 'worker exited without diagnostics'}
        return json.loads(proc.stdout)
    except subprocess.TimeoutExpired:
        return {'source': str(source.relative_to(ROOT)), 'error': 'validation/export timeout'}


def refresh_game(source, build, existing, schema=SCHEMA):
    """Revalidate and re-extract exactly the previously retained played states."""
    relative = str(source.relative_to(ROOT))
    rows = [r for line in (existing/'samples.jsonl').read_text().splitlines()
        if (r := json.loads(line))['source'] == relative]
    if not rows or any(r['source_sha256'] != digest(source) for r in rows):
        raise ValueError('missing samples or changed source game')
    rules = autoplay.load_rules(build)
    game = rules.game.from_pgn(source.read_text())
    if game.has_illegal_history():
        raise ValueError('illegal history')
    while game.has_parent():
        game.visit_parent()
    selected = {r['action_index']: r for r in rows}
    result = game.get_header('Result')
    for index in range(max(selected) + 1):
        children = game.get_child_actions()
        if len(children) != 1:
            raise ValueError('missing or ambiguous played continuation')
        if index in selected:
            row = selected[index]
            if (str(game.get_current_hash()) != row['state_hash']
                    or bool(game.get_current_present()[1]) != row['black_to_move']
                    or result != row['result']):
                raise ValueError('position identity or result changed')
            row['features'] = extract_features(game, schema)
        if index < max(selected) and not game.visit_child(children[0].act):
            raise ValueError('cannot replay validated main line')
    return {'source': relative, 'samples': rows}


def refresh(args):
    """Keep source-game splits and sample identities fixed across schema changes."""
    previous = json.loads((args.existing/'dataset.json').read_text())
    if digest(args.existing/'samples.jsonl') != previous['samples_sha256']:
        raise ValueError('existing sample hash mismatch')
    splits = json.loads((args.existing/'splits.json').read_text())
    old_rows = [json.loads(line) for line in (args.existing/'samples.jsonl').read_text().splitlines()]
    for row in old_rows:
        if splits[row['source']] != {k: row[k] for k in ('split', 'family')}:
            raise ValueError('existing split assignment mismatch')
    args.output.mkdir(parents=True, exist_ok=False)
    def worker(source):
        try:
            proc = subprocess.run([sys.executable, str(Path(__file__).resolve()), '_refresh',
                '--source', str(ROOT/source), '--build', str(args.build),
                '--existing', str(args.existing), '--schema', args.schema], capture_output=True, text=True,
                timeout=args.timeout)
            if proc.returncode:
                return {'source': source, 'exit_code': proc.returncode,
                    'error': proc.stderr[-3000:] or 'worker exited without diagnostics'}
            return json.loads(proc.stdout)
        except subprocess.TimeoutExpired:
            return {'source': source, 'error': 'refresh timeout'}
    kept, exclusions = [], []
    sources = sorted({r['source'] for r in old_rows})
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        for index, record in enumerate(executor.map(worker, sources), 1):
            if 'error' in record:
                exclusions.append(record)
            else:
                kept.extend(record['samples'])
            if index % 50 == 0:
                print(f'Refreshed {index}/{len(sources)} games; exclusions={len(exclusions)}', flush=True)
    kept.sort(key=lambda r: (r['source'], r['action_index']))
    with (args.output/'samples.jsonl').open('w') as output:
        for row in kept:
            output.write(json.dumps(row, sort_keys=True)+'\n')
    save(args.output/'splits.json', splits)
    save(args.output/'refresh-exclusions.json', exclusions)
    save(args.output/'exclusions.json', json.loads((args.existing/'exclusions.json').read_text()))
    by_split = {s: [r for r in kept if r['split'] == s] for s in ('train', 'validation', 'test')}
    feature_counts = Counter(tuple(r['features']) for r in kept)
    summary = dict(previous, schema=args.schema,
        previous_schema=previous['schema'], previous_dataset=str(args.existing),
        previous_samples_sha256=previous['samples_sha256'],
        splits_sha256=digest(args.output/'splits.json'),
        samples_sha256=digest(args.output/'samples.jsonl'),
        binding_sha256={p.name: digest(p) for p in args.build.glob('engine*.so')},
        refresh_excluded_games=len(exclusions), refresh_excluded_samples=len(old_rows)-len(kept),
        split_games={s: len({r['source'] for r in rows}) for s, rows in by_split.items()},
        split_families={s: len({r['family'] for r in rows}) for s, rows in by_split.items()},
        sample_counts={s: len(rows) for s, rows in by_split.items()},
        duplicate_feature_rows=len(kept)-len(feature_counts),
        duplicate_feature_rate=1-len(feature_counts)/len(kept) if kept else 0,
        refresh_policy='revalidate original sources and replay identical full-history states; '
            'preserve source-family assignments and labels; no resampling or added games',
        feature_policy='solid HC remove_check=true, prune_optional_moves=true; '
            'columns 62/63 log(max(1, product(axis sizes))) for playable/new axes separately'
            + ('; columns 70–79 orthogonal and diagonal royal rays' if args.schema != BASE_SCHEMA else '')
            + ('; columns 80–82 same-board protection tiers'
               if INPUT_COUNTS[args.schema] >= INPUT_COUNTS[PROTECTION_SCHEMA] else '')
            + ('; columns 83–84 distinct local checking moves with optional moves enabled'
               if INPUT_COUNTS[args.schema] >= INPUT_COUNTS[LOCAL_CHECK_SCHEMA] else '')
            + ('; columns 85–86 best physical and jump capture gains'
               if args.schema == SCHEMA else ''))
    save(args.output/'dataset.json', summary)
    print(json.dumps({k: summary[k] for k in ('schema', 'split_games', 'sample_counts',
        'refresh_excluded_games', 'refresh_excluded_samples', 'duplicate_feature_rate')}, indent=2))
    if not all(by_split.values()):
        raise ValueError('empty refreshed partition')


def prepare(args):
    args.output.mkdir(parents=True, exist_ok=False)
    paths = sorted((ROOT / 'test/pgn/online').glob('*.5dpgn'))
    paths = [p for p in paths if '[Variant "Standard - Turn Zero"]' in p.read_text()]
    records, exclusions = [], []
    explicit = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        eligible = []
        for path in paths:
            headers = re.findall(r'^\[Result "([^"\n]+)"\]', path.read_text(), re.M)
            if not headers:
                exclusions.append({'source': str(path.relative_to(ROOT)),
                    'error': 'missing explicit result header'})
            else:
                explicit += 1
                eligible.append(path)
        for index, row in enumerate(executor.map(lambda p: isolated(p, args), eligible), 1):
            (exclusions if 'error' in row else records).append(row)
            if index % 50 == 0:
                print(f'Validated {index}/{len(eligible)}; usable={len(records)}', flush=True)
    save(args.output / 'validated-games.json', records)
    save(args.output / 'exclusions.json', exclusions)
    # Conservative family: any identical full-history state after eight actions,
    # plus identical source bytes, ties the entire games together. This also
    # joins truncated/extended games and duplicate games with different headers.
    parent = list(range(len(records)))
    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    seen = {}
    for i, row in enumerate(records):
        keys = [('source', row['source_sha256'])] + [
            ('state', h['state_hash']) for h in row['history'][8:]]
        for key in keys:
            if key in seen:
                parent[find(i)] = find(seen[key])
            else:
                seen[key] = i
    families = defaultdict(list)
    for i, row in enumerate(records):
        families[find(i)].append(row)
    suite = json.loads(args.suite.read_text())
    suite_sources = {r['source'] for r in suite['positions']}
    # Hash suite roots too, even if a suite source failed this export.
    suite_hashes = {r['state_hash'] for r in suite['positions']}
    blocked, allowed = [], []
    for family in families.values():
        forbidden = any(r['source'] in suite_sources or any(
            h['state_hash'] in suite_hashes for h in r['history']) for r in family)
        (blocked if forbidden else allowed).append(family)
    rng = random.Random(args.seed)
    rng.shuffle(allowed)
    # Greedy game-count balancing with whole families, largest first; random
    # order resolves ties reproducibly, without consulting outcome or features.
    allowed.sort(key=len, reverse=True)
    total = sum(map(len, allowed))
    goals = dict(train=.8 * total, validation=.1 * total, test=.1 * total)
    counts = dict.fromkeys(goals, 0)
    assignments = {}
    partition = {}
    for family in allowed:
        split = max(goals, key=lambda s: goals[s] - counts[s])
        counts[split] += len(family)
        family_id = min(r['source'] for r in family)
        for row in family:
            assignments[row['source']] = {'split': split, 'family': family_id}
            partition[row['source']] = split
    for family in blocked:
        for row in family:
            assignments[row['source']] = {'split': 'canonical-excluded',
                'family': min(r['source'] for r in family)}
    # Exact state deduplication is global and label conflicts are excluded.
    # Family assignment precedes this and uses every validated state, not merely
    # the sampled rows. Thus no retained state can cross split boundaries.
    labels = defaultdict(set)
    raw = []
    for game in records:
        if game['source'] not in partition:
            continue
        for row in game['samples']:
            row = dict(row, source=game['source'], source_sha256=game['source_sha256'],
                **assignments[game['source']])
            labels[row['state_hash']].add(row['y_mover'])
            raw.append(row)
    conflicts = {h for h, ys in labels.items() if len(ys) > 1}
    kept, seen = [], set()
    for row in sorted(raw, key=lambda r: (r['source'], r['action_index'])):
        if row['state_hash'] in conflicts or row['state_hash'] in seen:
            continue
        seen.add(row['state_hash'])
        kept.append(row)
    with (args.output / 'samples.jsonl').open('w') as output:
        for row in kept:
            output.write(json.dumps(row, sort_keys=True) + '\n')
    save(args.output / 'splits.json', assignments)
    feature_counts = Counter(tuple(r['features']) for r in kept)
    summary = {'schema': args.schema, 'seed': args.seed, 'metadata_standard_games': len(paths),
        'metadata_with_result': explicit, 'metadata_without_result': len(paths)-explicit,
        'validated_games': len(records), 'validation_exclusions': len(exclusions),
        'validated_sample_count': sum(len(r['samples']) for r in records),
        'exclusion_reasons': dict(Counter(r['error'].strip().split('\n')[-1] for r in exclusions)),
        'canonical_source_games': len(suite_sources),
        'canonical_excluded_games_with_relatives': sum(map(len, blocked)),
        'canonical_excluded_sample_count': sum(len(r['samples']) for f in blocked for r in f),
        'allowed_families': len(allowed), 'family_sizes': sorted(map(len, allowed), reverse=True),
        'split_games': counts, 'split_families': dict(Counter(
            next(iter({partition[r['source']] for r in f})) for f in allowed)),
        'sample_counts': dict(Counter(r['split'] for r in kept)),
        'raw_sample_counts': dict(Counter(r['split'] for r in raw)),
        'duplicate_state_rows': len(raw) - len(labels),
        'duplicate_state_rate': 1-len(labels)/len(raw) if raw else 0,
        'conflicting_states_excluded': len(conflicts),
        'conflicting_rows_excluded': sum(r['state_hash'] in conflicts for r in raw),
        'duplicate_feature_rows': len(kept)-len(feature_counts),
        'duplicate_feature_rate': 1-len(feature_counts)/len(kept) if kept else 0,
        'width_counts': dict(Counter(r['width'] for r in kept)),
        'phase_counts': dict(Counter(min(4,5*r['action_index']//r['total_actions']) for r in kept)),
        'labels_by_split': {s: dict(Counter(r['y_mover'] for r in kept if r['split']==s)) for s in goals},
        'suite_sha256': digest(args.suite), 'samples_sha256': digest(args.output/'samples.jsonl'),
        'binding_sha256': {p.name: digest(p) for p in args.build.glob('engine*.so')},
        'source_sha256': {str(p.relative_to(ROOT)): digest(p) for p in paths},
        'policy': 'whole-game families by any shared full-history state at action>=8; '
                  'canonical families excluded from all supervised splits; no branch labels; '
                  'global exact-state dedup and conflicting-label rejection'}
    save(args.output / 'dataset.json', summary)
    print(json.dumps({k:v for k,v in summary.items() if k not in ('source_sha256','family_sizes','exclusion_reasons')}, indent=2))
    if any(counts[s] == 0 or not any(r['split']==s for r in kept) for s in goals):
        raise ValueError('empty partition; revise the dataset policy explicitly')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('prepare', 'refresh', '_collect', '_refresh'))
    parser.add_argument('--source', type=Path)
    parser.add_argument('--build', type=Path, default=ROOT/'build-canonical')
    parser.add_argument('--suite', type=Path, default=ROOT/'test/engine_suite/v1/manifest.json')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--schema', choices=INPUT_COUNTS, default=SCHEMA)
    parser.add_argument('--existing', type=Path, help='existing dataset whose splits and samples are preserved')
    parser.add_argument('--samples', type=int, default=20)
    parser.add_argument('--seed', type=int, default=20261106)
    parser.add_argument('--jobs', type=int, default=5)
    parser.add_argument('--timeout', type=float, default=20)
    args = parser.parse_args()
    for key, value in vars(args).items():
        if isinstance(value, Path):
            setattr(args, key, value.resolve())
    if min(args.samples, args.jobs, args.timeout) <= 0:
        parser.error('samples, jobs and timeout must be positive')
    if args.action in ('prepare', 'refresh'):
        rules = autoplay.load_rules(args.build)
        if not hasattr(rules.game, FEATURE_GETTERS[args.schema]):
            parser.error(f'the Python binding does not support {args.schema}')
    if args.action == '_collect':
        print(json.dumps(collect(args.source, args.build, args.samples, args.seed, args.schema)))
    elif args.action == '_refresh':
        print(json.dumps(refresh_game(args.source, args.build, args.existing, args.schema)))
    elif args.action == 'refresh':
        if args.output is None or args.existing is None:
            parser.error('refresh requires --output and --existing')
        refresh(args)
    else:
        if args.output is None:
            parser.error('prepare requires --output')
        prepare(args)


if __name__ == '__main__':
    main()

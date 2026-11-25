#!/usr/bin/env python3
"""Train, select on validation, freeze and test a position model.

Requires numpy and PyTorch in the chosen Python. Native inference has no such
runtime dependency. Test metrics are computed only for the selected checkpoint.
"""
from __future__ import annotations

import argparse
import copy
from collections import Counter
import hashlib
import json
from pathlib import Path
import platform
import sys

import numpy as np
import torch
from torch import nn

from neural_dataset import BASE_SCHEMA, INPUT_COUNTS, digest, save


class Network(nn.Module):
    def __init__(self, input_count=INPUT_COUNTS[BASE_SCHEMA]):
        super().__init__()
        self.layers = nn.Sequential(nn.Linear(input_count, 32), nn.ReLU(),
            nn.Linear(32, 16), nn.ReLU(), nn.Linear(16, 1))

    def forward(self, x):
        return self.layers(x).squeeze(-1)  # z; inference is tanh(z)


def mover_label(result, black):
    white = {'1-0': 1, '1/2-1/2': 0, '0-1': -1}[result]
    return -white if black else white


def normalization(x, sample_weight=None):
    if x.ndim != 2 or x.shape[1] not in INPUT_COUNTS.values() or not np.isfinite(x).all():
        raise ValueError('normalization requires finite data with a supported input count')
    values = x.astype(np.float64)
    if sample_weight is None:
        mean = values.mean(axis=0).astype(np.float32)
        std = values.std(axis=0)
    else:
        weights = np.asarray(sample_weight, dtype=np.float64)
        if weights.shape != (len(x),) or not np.isfinite(weights).all() or (weights <= 0).any():
            raise ValueError('normalization requires positive finite row weights')
        center = np.average(values, weights=weights, axis=0)
        mean = center.astype(np.float32)
        std = np.sqrt(np.average((values-center)**2, weights=weights, axis=0))
    constant = (x.max(axis=0) == x.min(axis=0))
    inverse = np.zeros(x.shape[1], dtype=np.float32)
    inverse[~constant] = (1 / std[~constant]).astype(np.float32)
    if not np.isfinite(mean).all() or not np.isfinite(inverse).all():
        raise ValueError('nonfinite normalization statistics')
    return mean, inverse, constant


def family_weights(rows):
    counts = Counter(row['family'] for row in rows)
    return np.asarray([len(rows)/(len(counts)*counts[row['family']]) for row in rows],
        dtype=np.float32)


def feature_columns(name, input_count=INPUT_COUNTS[BASE_SCHEMA]):
    if name == 'all':
        return list(range(input_count))
    if name == 'without-rays':
        return list(range(INPUT_COUNTS[BASE_SCHEMA]))
    relative = list(range(9,17))+list(range(25,33))+list(range(41,49))+[58]+list(range(64,70))
    if name == 'differences':
        return relative
    if name == 'relative-volume':
        return sorted(relative+[62,63])
    raise ValueError('unknown neural feature set')


def normalize(x, mean, inverse):
    if not np.isfinite(x).all():
        raise ValueError('nonfinite features')
    normalized = (x - mean) * inverse
    if not np.isfinite(normalized).all():
        raise ValueError('nonfinite normalized features')
    return torch.from_numpy(normalized)


def export_model(path, model, mean, inverse, schema=BASE_SCHEMA):
    input_count = INPUT_COUNTS[schema]
    parameters = [p.detach().cpu().numpy() for p in model.parameters()]
    if (mean.shape != (input_count,) or inverse.shape != mean.shape
            or [p.shape for p in parameters] != [(32, input_count), (32,), (16, 32), (16,), (1, 16), (1,)]):
        raise ValueError('wrong architecture')
    with Path(path).open('w') as output:
        output.write(f'5DNEURAL 1\n{schema}\n{input_count} 32 16 1\n')
        for array in [mean, inverse, *parameters]:
            array = np.asarray(array, dtype=np.float32)
            if not np.isfinite(array).all():
                raise ValueError('nonfinite model')
            output.write(' '.join(format(float(v), '.9g') for v in array.ravel()) + '\n')


@torch.no_grad()
def metrics(model, x, y, sample_weight=None):
    z = model(x)
    probability = torch.sigmoid(2*z)
    value = torch.tanh(z)
    result = {'bce': float(nn.functional.binary_cross_entropy_with_logits(2*z, y)),
        'brier': float(((probability-y)**2).mean()),
        'value_mse': float(((value-(2*y-1))**2).mean()),
        'decisive_accuracy': float(((probability[y!=.5]>=.5)==(y[y!=.5]>.5)).float().mean()),
        'saturation_abs_ge_0.9': float((value.abs()>=.9).float().mean()),
        'saturation_abs_ge_0.99': float((value.abs()>=.99).float().mean()),
        'value_mean': float(value.mean()), 'value_std': float(value.std(unbiased=False)),
        'value_quantiles': np.quantile(value.numpy(), [0,.05,.25,.5,.75,.95,1]).tolist()}
    if sample_weight is not None:
        result['family_weighted_bce'] = float((nn.functional.binary_cross_entropy_with_logits(
            2*z, y, reduction='none')*sample_weight).mean())
    return result


def constant_metrics(p, y):
    probabilities = torch.full_like(y, p)
    return {'probability': p, 'value': 2*p-1,
        'bce': float(nn.functional.binary_cross_entropy(probabilities,y)),
        'brier': float(((probabilities-y)**2).mean()),
        'value_mse': float(((2*probabilities-2*y)**2).mean()),
        'decisive_accuracy': float(((probabilities[y!=.5]>=.5)==(y[y!=.5]>.5)).float().mean())}


def train(args):
    args.output.mkdir(parents=True, exist_ok=False)
    torch.set_num_threads(1)
    torch.use_deterministic_algorithms(True)
    rows = [json.loads(line) for line in (args.dataset/'samples.jsonl').read_text().splitlines()]
    manifest = json.loads((args.dataset/'dataset.json').read_text())
    if digest(args.dataset/'samples.jsonl') != manifest['samples_sha256']:
        raise ValueError('dataset hash mismatch')
    schema = manifest['schema']
    if schema not in INPUT_COUNTS:
        raise ValueError('feature schema mismatch')
    input_count = INPUT_COUNTS[schema]
    # Audit provenance and perspective independently of the stored numeric label.
    family_splits, state_splits = {}, {}
    for row in rows:
        if row['y_mover'] != mover_label(row['result'], row['black_to_move']):
            raise ValueError('label perspective mismatch')
        for key, seen in ((row['family'],family_splits), (row['state_hash'],state_splits)):
            if key in seen and seen[key] != row['split']:
                raise ValueError('split leakage')
            seen[key] = row['split']
    data = {s: [r for r in rows if r['split']==s] for s in ('train','validation','test')}
    if not all(data.values()):
        raise ValueError('empty supervised partition')
    raw = {s: np.asarray([r['features'] for r in rs],dtype=np.float32) for s,rs in data.items()}
    if any(array.shape != (len(data[s]), input_count) for s, array in raw.items()):
        raise ValueError('dataset input count does not match the feature schema')
    weights = {s: torch.from_numpy(family_weights(rows)) for s, rows in data.items()} if args.family_balanced else {}
    mean, inverse, constant = normalization(raw['train'],
        weights['train'].numpy() if args.family_balanced else None)
    retained = set(feature_columns(args.feature_set, input_count))
    masked = [i for i in range(input_count) if i not in retained]
    inverse[masked] = 0
    # The test partition stays untouched by fitting and checkpoint selection.
    x = {s: normalize(raw[s],mean,inverse) for s in ('train','validation')}
    y = {s: torch.tensor([(r['y_mover']+1)/2 for r in rs], dtype=torch.float32) for s,rs in data.items()}
    constant_p = float((y['train']*weights['train']).mean()) if args.family_balanced else float(y['train'].mean())
    save(args.output/'normalization.json', {'mean':mean.tolist(),'inverse_scale':inverse.tolist(),
        'constant_columns':np.flatnonzero(constant).tolist(),
        'masked_columns':masked, 'feature_set':args.feature_set,
        'family_balanced':args.family_balanced,
        'policy':'training population mean/std; family-weighted when enabled; constant and masked columns normalize to zero'})
    candidates, curves = [], []
    for seed in args.seeds:
        torch.manual_seed(seed)
        model = Network(input_count)
        optimizer = torch.optim.AdamW(model.parameters(),lr=1e-3,weight_decay=args.weight_decay)
        generator = torch.Generator().manual_seed(seed)
        best, stale, best_state, best_epoch = float('inf'), 0, None, 0
        for epoch in range(1,args.epochs+1):
            model.train()
            for indices in torch.randperm(len(x['train']),generator=generator).split(256):
                optimizer.zero_grad(set_to_none=True)
                z = model(x['train'][indices])
                loss = nn.functional.binary_cross_entropy_with_logits(2*z,y['train'][indices],
                    reduction='none')
                if args.family_balanced:
                    loss = loss*weights['train'][indices]
                loss = loss.mean()
                if not torch.isfinite(loss):
                    raise ValueError('nonfinite loss')
                loss.backward()
                optimizer.step()
            model.eval()
            record = {'seed':seed,'epoch':epoch,'train':metrics(model,x['train'],y['train'],weights.get('train')),
                'validation':metrics(model,x['validation'],y['validation'],weights.get('validation'))}
            curves.append(record)
            current = record['validation']['family_weighted_bce' if args.family_balanced else 'bce']
            print(f'seed={seed} epoch={epoch} train={record["train"]["bce"]:.6f} val={current:.6f}',flush=True)
            if current < best:
                best, stale, best_epoch = current, 0, epoch
                best_state = copy.deepcopy(model.state_dict())
            else:
                stale += 1
                if stale >= 5:
                    break
        model.load_state_dict(best_state)
        export_model(args.output/f'seed-{seed}.nn',model,mean,inverse,schema)
        candidates.append({'seed':seed,'epoch':best_epoch,'validation_bce':best,
            'epochs_run':epoch,'state':best_state})
    selected = min(candidates,key=lambda c:c['validation_bce'])
    model = Network(input_count)
    model.load_state_dict(selected['state'])
    model.eval()
    export_model(args.output/'model.nn',model,mean,inverse,schema)
    # One post-selection test evaluation; no per-seed test scores are produced.
    evaluated = ('train','validation') if args.defer_test else ('train','validation','test')
    if not args.defer_test:
        x['test'] = normalize(raw['test'],mean,inverse)
    results = {s: metrics(model,x[s],y[s],weights.get(s)) for s in evaluated}
    controls = {s: constant_metrics(constant_p,y[s]) for s in evaluated}
    import autoplay
    native = autoplay.load_rules(args.build).neural_model.load(str((args.output/'model.nn').resolve()))
    examples = []
    with torch.no_grad():
        expected = torch.tanh(model(x['train'][:512])).numpy()
    for row, want in zip(data['train'][:512],expected):
        actual = native.evaluate_features(row['features'])
        examples.append({'source':row['source'],'action_index':row['action_index'],
            'state_hash':row['state_hash'],'features':row['features'],
            'python':float(want),'cpp':actual,'absolute_error':abs(actual-float(want))})
    max_error = max(e['absolute_error'] for e in examples)
    save(args.output/'parity-examples.json',examples)
    if max_error > 1e-5:
        raise ValueError(f'native parity error {max_error}')
    save(args.output/'learning-curves.json',curves)
    report = {'feature_schema':schema,
        'architecture':[input_count,32,16,1],'activations':['ReLU','ReLU','tanh'],
        'parameters':sum(p.numel() for p in model.parameters()),
        'optimizer':{'name':'AdamW','learning_rate':1e-3,'weight_decay':args.weight_decay,'batch_size':256},
        'family_balanced':args.family_balanced,'feature_set':args.feature_set,
        'masked_columns':masked,
        'selection_metric':'family_weighted_bce' if args.family_balanced else 'bce',
        'max_epochs':args.epochs,'patience':5,'seeds':args.seeds,
        'loss':'BCEWithLogits(2*z, (y_mover+1)/2)',
        'checkpoints':[{k:v for k,v in c.items() if k!='state'} for c in candidates],
        'selected':{k:v for k,v in selected.items() if k!='state'},
        'metrics':results,'training_mean_constant':controls,
        'test_policy':'deferred; not evaluated' if args.defer_test else 'one evaluation of validation-selected checkpoint after all seed selection',
        'native_parity_max_absolute_error':max_error,'native_parity_examples':len(examples),
        'normalization_sha256':digest(args.output/'normalization.json'),
        'model_sha256':digest(args.output/'model.nn'),
        'samples_sha256':digest(args.dataset/'samples.jsonl'),
        'splits_sha256':digest(args.dataset/'splits.json'),
        'script_sha256':digest(__file__), 'python':sys.version,'torch':torch.__version__,
        'numpy':np.__version__,'platform':platform.platform()}
    save(args.output/'training.json',report)
    print(json.dumps(report,indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dataset',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--build',type=Path,default=Path(__file__).resolve().parents[1]/'build-canonical')
    parser.add_argument('--seeds',type=int,nargs='+',default=[17,42,2026])
    parser.add_argument('--epochs',type=int,default=40)
    parser.add_argument('--weight-decay',type=float,default=1e-4)
    parser.add_argument('--family-balanced',action='store_true',help='equal total loss and normalization weight per source-game family')
    parser.add_argument('--feature-set',choices=('all','without-rays','differences','relative-volume'),default='all')
    parser.add_argument('--defer-test',action='store_true',help='do not evaluate the test partition during model development')
    args = parser.parse_args()
    if not 1 <= args.epochs <= 40 or len(set(args.seeds)) != len(args.seeds):
        parser.error('use 1–40 epochs and distinct initialization seeds')
    if not np.isfinite(args.weight_decay) or args.weight_decay < 0:
        parser.error('weight decay must be finite and nonnegative')
    for key in ('dataset','output','build'):
        setattr(args,key,getattr(args,key).resolve())
    train(args)


if __name__ == '__main__':
    main()

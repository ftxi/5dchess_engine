#!/usr/bin/env python3
"""Paired canonical matches with equal fixed iterations or fixed search time.

Iteration mode sends only ``go depth N`` (ten iterations per depth unit), never
an engine movetime cutoff. The external watchdog detects hung searches; a legal
nonterminal response with a different iteration count invalidates its pair.
"""
from __future__ import annotations

import argparse
import asyncio
from collections import Counter
import csv
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import shlex
import shutil
import statistics
import sys
import time

import autoplay
import canonical_test as canonical

ROOT = Path(__file__).resolve().parents[1]
SEARCH_FIELDS = ('pair', 'position_id', 'game', 'action', 'role', 'color', 'seed',
    'mode', 'requested_iterations', 'requested_movetime_ms', 'actual_iterations',
    'elapsed_seconds', 'wall_seconds', 'ips', 'shortfall', 'failure', 'state_hash', 'timeline_width',
    'terminal_tree_evaluations', 'inconclusive_rollouts', 'conclusive_rollouts', 'score_average',
    'terminal_root', 'amaf_lookups', 'amaf_shared_uses', 'amaf_changed_values',
    'amaf_coordinate_updates', 'amaf_action_updates', 'amaf_nonzero_coordinate_updates', 'amaf_tables',
    'amaf_ordering_rebuilds', 'amaf_ordering_changed_axes',
    'amaf_evaluation_cache_hits', 'amaf_evaluation_cache_misses',
    'amaf_child_choices', 'amaf_changed_child_choices',
    'amaf_unique_evidence_updates', 'amaf_repeated_evidence_skips',
    'neural_cache_hits', 'neural_cache_misses', 'neural_prior_choices',
    'neural_changed_choices', 'neural_unique_updates', 'neural_repeated_skips')
GAME_FIELDS = ('pair', 'position_id', 'game', 'target_color', 'seed', 'outcome',
    'actions', 'reason', 'failure', 'pgn_path')


def save(path, value):
    canonical.save(path, value)


def readonly_copy(source, target):
    expected = canonical.digest(source)
    shutil.copy2(source, target)
    if canonical.digest(source) != expected or canonical.digest(target) != expected:
        raise ValueError('artifact changed during snapshot')
    target.chmod(target.stat().st_mode & ~0o222)
    return expected


def seeded_command(command, seed):
    tokens = shlex.split(command)
    flags = [i for i, t in enumerate(tokens) if t in ('--seed', '-s')]
    if flags:
        if len(flags) != 1 or flags[0]+1 >= len(tokens) or tokens[flags[0]+1] != '{seed}':
            raise ValueError('engine commands must omit --seed or use --seed {seed}')
        tokens[flags[0]+1] = str(seed)
    else:
        if any('{seed}' in t for t in tokens):
            raise ValueError('{seed} must be the argument to --seed')
        tokens.extend(('--seed', str(seed)))
    return shlex.join(tokens)


def pair_seed(seed, position_id):
    return int.from_bytes(hashlib.sha256(f'{seed}:{position_id}'.encode()).digest()[:4], 'little')


class FixedEngineProcess(autoplay.EngineProcess):
    def _record(self, direction, line):
        super()._record(direction, line)
        if direction == '<' and line.startswith('info amaf_stats '):
            self.last_amaf_stats = dict(token.split('=', 1)
                for token in line.split()[2:] if '=' in token)
        if direction == '<' and line.startswith('info neural_stats '):
            self.last_neural_stats = dict(token.split('=', 1)
                for token in line.split()[2:] if '=' in token)

    async def choose_budget(self, position, history, iterations, movetime, watchdog):
        self.phase = 'synchronizing_position'
        request = f'position {position}'
        if history:
            request += ' moves ' + ' '.join(history)
        await self.send(request)
        await self.send('isready')
        await self.wait_for('readyok', self.timeout_seconds)
        self.last_engine_stats = {}
        self.last_amaf_stats = {}
        self.last_neural_stats = {}
        self.last_engine_score = ''
        self.last_engine_scores = ''
        self.last_go_seconds = None
        self.phase = 'searching'
        command = f'go depth {iterations // 10}' if iterations is not None else f'go movetime {movetime}'
        started = time.perf_counter()
        try:
            await self.send(command)
            response = await self.wait_for(('bestmove', 'nobestmove'), watchdog)
        finally:
            self.last_go_seconds = time.perf_counter()-started
        self.phase = 'idle'
        return None if response == 'nobestmove' else response.split()[1:] or None


@dataclass
class GameResult:
    pair: int
    position_id: str
    game: int
    target_color: str
    seed: int
    outcome: str
    actions: int
    reason: str
    failure: str
    pgn_path: str

    def as_dict(self):
        return vars(self).copy()


def outcome_for_status(status, target_white):
    if status == 'draw':
        return 'draw'
    return 'win' if (status == 'white') == target_white else 'loss'


def descriptive_score(games):
    counts = Counter(g['outcome'] if isinstance(g, dict) else g.outcome for g in games)
    count = counts['win']+counts['draw']+counts['loss']
    return {'games': count, 'wins': counts['win'], 'draws': counts['draw'],
        'losses': counts['loss'], 'void': counts['void'],
        'score': (counts['win']+.5*counts['draw'])/count if count else None}


def numeric_stats(values):
    values = list(values)
    if not values:
        return {'count': 0}
    return {'count': len(values), 'total': sum(values), 'mean': statistics.mean(values),
        'median': statistics.median(values), 'min': min(values), 'max': max(values)}


class Experiment:
    def __init__(self, args, rows, commands, rules):
        self.args, self.rows, self.commands, self.rules = args, rows, commands, rules
        self.games = []
        self.searches = []
        self.completed = {}
        self.next_commit = 1
        self.committed = []
        self.launches = []
        self.started = time.perf_counter()
        for name, fields in (('games.csv', GAME_FIELDS), ('searches.csv', SEARCH_FIELDS)):
            with (args.output/name).open('w', newline='') as output:
                csv.DictWriter(output, fieldnames=fields).writeheader()

    def append_csv(self, name, fields, row):
        with (self.args.output/name).open('a', newline='') as output:
            csv.DictWriter(output, fieldnames=fields).writerow(row)

    def record_search(self, row):
        self.searches.append(row)
        self.append_csv('searches.csv', SEARCH_FIELDS, row)

    def record_game(self, result):
        self.games.append(result)
        self.append_csv('games.csv', GAME_FIELDS, result.as_dict())

    async def adjudicate(self, game, game_number, action):
        snapshot = autoplay.snapshot_pgn(game)
        file = self.args.output/f'adjudication-{game_number:04d}-{action:03d}.5dpgn'
        file.write_text(snapshot)
        try:
            return await asyncio.wait_for(autoplay.adjudicate(snapshot, self.args.rules_dir),
                self.args.adjudication_watchdog)
        except TimeoutError:
            return 'error:adjudication timeout'

    async def game(self, pair, row, target_white):
        args, rules = self.args, self.rules
        number = 2*(pair-1)+(1 if target_white else 2)
        seed = pair_seed(args.seed, row['id'])
        engines = {role: FixedEngineProcess(role,
            seeded_command(command, seed), args.protocol_timeout)
            for role, command in self.commands.items()}
        players = [('target', engines['target']), ('baseline', engines['baseline'])]
        if not target_white:
            players.reverse()
        for role, engine in engines.items():
            launch = {'pair': pair, 'position_id': row['id'], 'game': number,
                'role': role, 'seed': seed, 'command': engine.command}
            self.launches.append(launch)
            with (args.output/'engine-launches.jsonl').open('a') as output:
                output.write(json.dumps(launch, sort_keys=True)+'\n')
        game = rules.game.from_pgn((args.output/'openings'/f"{row['id']}.5dpgn").read_text())
        metadata = dict(game.metadata)
        metadata.update(event='Fixed-budget canonical screen', site='Local',
            date=datetime.now(timezone.utc).strftime('%Y.%m.%d'), round=str(number),
            white=players[0][0], black=players[1][0], result='*')
        game.metadata = metadata
        initial = autoplay.uci_position(game)
        history = []
        outcome, reason, failure, actions = 'void', 'game did not start', '', 0
        current_role = 'controller'
        try:
            await asyncio.gather(*(engine.start() for engine in engines.values()))
            await asyncio.gather(*(engine.new_game() for engine in engines.values()))
            for action in range(1, args.max_actions+1):
                actions = action
                _, black = game.get_current_present()
                current_role, player = players[int(black)]
                search = {'pair': pair, 'position_id': row['id'], 'game': number,
                    'action': action, 'role': current_role, 'color': 'black' if black else 'white',
                    'seed': seed, 'mode': 'iterations' if args.iterations is not None else 'time',
                    'requested_iterations': args.iterations if args.iterations is not None else '',
                    'requested_movetime_ms': args.movetime if args.movetime is not None else '',
                    'actual_iterations': '', 'elapsed_seconds': '', 'wall_seconds': '',
                    'ips': '', 'shortfall': '', 'failure': '',
                    'state_hash': str(game.get_current_hash()),
                    'timeline_width': sum(map(len, game.get_current_timeline_status())),
                    'terminal_tree_evaluations': '', 'inconclusive_rollouts': '',
                    'conclusive_rollouts': '', 'score_average': '', 'terminal_root': 0,
                    **{f'amaf_{key}': '' for key in ('lookups', 'shared_uses', 'changed_values',
                        'coordinate_updates', 'action_updates', 'nonzero_coordinate_updates', 'tables',
                        'ordering_rebuilds', 'ordering_changed_axes',
                        'evaluation_cache_hits', 'evaluation_cache_misses',
                        'child_choices', 'changed_child_choices',
                        'unique_evidence_updates', 'repeated_evidence_skips')}}
                try:
                    moves = await player.choose_budget(initial, history, args.iterations,
                        args.movetime, args.watchdog)
                except (autoplay.ProtocolError, ValueError) as exc:
                    failure = 'protocol'
                    reason = f'{current_role} failure: {exc}'
                    search['failure'] = failure
                    search['wall_seconds'] = player.last_go_seconds or ''
                    self.record_search(search)
                    outcome = 'loss' if current_role == 'target' else 'win'
                    break
                stats = player.last_engine_stats
                search['wall_seconds'] = player.last_go_seconds
                search['elapsed_seconds'] = stats.get('elapsed_seconds', '')
                search['ips'] = stats.get('ips', '')
                for field in ('terminal_tree_evaluations', 'inconclusive_rollouts', 'conclusive_rollouts'):
                    search[field] = stats.get(field, '')
                search['score_average'] = player.last_engine_score
                for key, value in player.last_amaf_stats.items():
                    field = 'amaf_'+key
                    if field in SEARCH_FIELDS:
                        search[field] = value
                for key, value in player.last_neural_stats.items():
                    field = 'neural_'+key
                    if field in SEARCH_FIELDS:
                        search[field] = value
                try:
                    actual = int(stats['iterations'])
                    if actual < 0:
                        raise ValueError('negative iteration count')
                    search['actual_iterations'] = actual
                except (KeyError, ValueError):
                    actual = None
                if not moves:
                    status = await self.adjudicate(game, number, action)
                    if status in ('white', 'black', 'draw'):
                        outcome = outcome_for_status(status, target_white)
                        reason = f'terminal position: {status}'
                        search['terminal_root'] = 1
                        # A terminal root may stop early. Independent rules
                        # adjudication, not the engine's claim, grants exemption.
                    else:
                        failure = 'no_move' if status == 'playing' else 'adjudication'
                        outcome = 'loss' if current_role == 'target' else 'win'
                        if failure == 'adjudication':
                            outcome = 'void'
                        reason = f'{current_role} returned no move; status={status}'
                        search['failure'] = failure
                    self.record_search(search)
                    break
                try:
                    duration = float(search['elapsed_seconds'])
                    valid_duration = math.isfinite(duration) and duration > 0
                except (TypeError, ValueError):
                    valid_duration = False
                if actual is None or actual == 0 or not valid_duration:
                    failure, reason = 'metrics', f'{current_role} missing usable iteration/duration metrics'
                elif args.iterations is not None and actual != args.iterations:
                    failure, reason = 'iteration_budget', (
                        f'{current_role} completed {actual}, requested {args.iterations} iterations')
                    search['shortfall'] = int(actual < args.iterations)
                else:
                    if args.iterations is not None:
                        search['shortfall'] = 0
                    error = autoplay.apply_action(rules, game, moves)
                    if error:
                        failure, reason = 'legality', f'{current_role} illegal action: {error}'
                search['failure'] = failure
                self.record_search(search)
                if failure:
                    outcome = 'loss' if current_role == 'target' else 'win'
                    break
                history.extend(moves)
                history.append('submit')
            else:
                outcome, reason = 'draw', 'action limit reached'
        except asyncio.CancelledError:
            outcome, failure, reason = 'void', 'interrupted', 'controller interrupted'
            raise
        except Exception as exc:
            failure, reason, outcome = 'controller', f'controller error: {type(exc).__name__}: {exc}', 'void'
        finally:
            await asyncio.gather(*(engine.close() for engine in engines.values()), return_exceptions=True)
            if failure:
                diagnostic = {'pair': pair, 'game': number, 'role': current_role,
                    'failure': failure, 'reason': reason,
                    'engines': {role: engine.diagnostic() for role, engine in engines.items()}}
                save(args.output/f'failure-{number:04d}.json', diagnostic)
            autoplay.set_pgn_result(game, 'draw' if outcome == 'draw' else
                ('white' if (outcome == 'win') == target_white else 'black')
                if outcome in ('win', 'loss') else 'protocol')
            autoplay.append_termination_comment(game, reason)
            path = args.output/f'pair-{pair:04d}-target-{"white" if target_white else "black"}.5dpgn'
            path.write_text(game.show_pgn(rules.SHOW_CAPTURE | rules.SHOW_PROMOTION | rules.SHOW_SHORT))
            result = GameResult(pair, row['id'], number, 'white' if target_white else 'black',
                seed, outcome, actions, reason, failure, str(path))
            self.record_game(result)
        return result

    async def pair(self, pair, row):
        return [await self.game(pair, row, True), await self.game(pair, row, False)]

    def commit(self, pair, games):
        self.completed[pair] = games
        while self.next_commit in self.completed:
            self.committed.append(self.completed.pop(self.next_commit))
            self.next_commit += 1
        summary = self.summary()
        save(self.args.output/'summary.json', summary)
        clean = summary['failure_free']
        score = f"{100*clean['score']:.2f}%" if clean['score'] is not None else 'n/a'
        print(f"pairs={summary['committed_prefix_pairs']}/{len(self.rows)} "
            f"valid={summary['valid_pairs']} failures={summary['failure_pairs']} "
            f"W:D:L={clean['wins']}:{clean['draws']}:{clean['losses']} score={score} "
            f"elapsed={time.perf_counter()-self.started:.1f}s", flush=True)

    def summary(self):
        committed_games = [g for pair in self.committed for g in pair]
        clean_pairs = [pair for pair in self.committed if all(
            not g.failure and g.outcome != 'void' for g in pair)]
        clean_games = [g for pair in clean_pairs for g in pair]
        failures = [pair[0].pair for pair in self.committed if any(g.failure for g in pair)]
        values = [sum({'win': 1, 'draw': .5, 'loss': 0}[g.outcome] for g in pair)/2
            for pair in clean_pairs]
        ci = None
        if len(values) > 1:
            radius = 1.96*statistics.stdev(values)/math.sqrt(len(values))
            ci = [max(0, statistics.mean(values)-radius), min(1, statistics.mean(values)+radius)]
        selected_searches = [s for s in self.searches if int(s['pair']) < self.next_commit]
        roles = {}
        for role in ('target', 'baseline'):
            searches = [s for s in selected_searches if s['role'] == role]
            roles[role] = {'nonterminal_iterations': numeric_stats(s['actual_iterations'] for s in searches
                    if not s['terminal_root'] and isinstance(s['actual_iterations'], int)),
                'terminal_root_searches': sum(s['terminal_root'] for s in searches),
                'iterations': numeric_stats(s['actual_iterations'] for s in searches
                    if isinstance(s['actual_iterations'], int)),
                'searches': len(searches),
                'shortfalls': sum(s['shortfall'] == 1 for s in searches),
                'budget_mismatches': sum(s['failure'] == 'iteration_budget' for s in searches),
                'failures': dict(Counter(s['failure'] for s in searches if s['failure'])),
                'wall_seconds': numeric_stats(s['wall_seconds'] for s in searches
                    if isinstance(s['wall_seconds'], (float, int)))}
        return {'schema_version': 1, 'requested_pairs': len(self.rows),
            'committed_prefix_pairs': len(self.committed), 'valid_pairs': len(clean_pairs),
            'failure_pairs': len(failures), 'failure_pair_ids': failures,
            'void_pairs': sum(any(g.outcome == 'void' for g in pair) for pair in self.committed),
            'completed_pairs_outside_prefix': sorted(self.completed),
            'all_finished': descriptive_score(self.games),
            'committed_including_failures': descriptive_score(committed_games),
            'failure_free': descriptive_score(clean_games),
            'pair_score_95pct_normal_interval_descriptive': ci,
            'interval_note': 'Descriptive paired normal interval; no optional-stopping or tuning correction.',
            'action_limit_draws': sum(g.reason == 'action limit reached' for g in committed_games),
            'failure_types': dict(Counter(g.failure for g in committed_games if g.failure)),
            'searches': roles, 'elapsed_seconds': time.perf_counter()-self.started}

    async def run(self):
        queue = asyncio.Queue()
        for pair, row in enumerate(self.rows, 1):
            queue.put_nowait((pair, row))
        async def worker():
            while not queue.empty():
                try:
                    pair, row = queue.get_nowait()
                except asyncio.QueueEmpty:
                    return
                result = await self.pair(pair, row)
                self.commit(pair, result)
        workers = [asyncio.create_task(worker()) for _ in range(min(self.args.jobs, len(self.rows)))]
        try:
            await asyncio.gather(*workers)
        finally:
            for worker_task in workers:
                worker_task.cancel()
            await asyncio.gather(*workers, return_exceptions=True)
            save(self.args.output/'summary.json', self.summary())


def prepare(args):
    manifest = canonical.verify(args.suite)
    reference = json.loads((args.reference/'reference.json').read_text())
    reference_binary = args.reference/reference.get('binary_name', '5dchess')
    if canonical.digest(reference_binary) != reference['binary_sha256']:
        raise ValueError('frozen reference binary changed')
    if reference['suite_sha256'] != canonical.digest(args.suite/'manifest.json'):
        raise ValueError('reference belongs to another canonical suite')
    canonical.verify_engine_assets(args.reference, reference.get('assets', []))
    by_id = {r['id']: r for r in manifest['positions']}
    rows = [by_id[i] for i in manifest['subsets'][args.subset]][args.skip_first:]
    if args.limit is not None:
        rows = rows[:args.limit]
    if not rows:
        raise ValueError('position selection is empty')
    # Reject command seed errors before creating any output artifacts.
    seeded_command(args.engine, args.seed)
    seeded_command(reference['engine'], args.seed)
    record = canonical.build_record(args.build)
    args.output.mkdir(parents=True, exist_ok=False)
    openings = args.output/'openings'
    openings.mkdir()
    for row in rows:
        shutil.copy2(args.suite/row['file'], openings/f"{row['id']}.5dpgn")
    rules_dir = args.output/'rules'
    rules_dir.mkdir()
    binding_files = [p for pattern in ('engine*.so', 'engine*.pyd') for p in args.rules_dir.glob(pattern)]
    if not binding_files:
        raise ValueError('build the Python rules binding first')
    binding_hashes = {p.name: readonly_copy(p, rules_dir/p.name) for p in binding_files}
    rules_source_dir = str(args.rules_dir)
    candidate = args.output/('candidate'+args.binary.suffix)
    baseline = args.output/('baseline'+reference_binary.suffix)
    candidate_hash = readonly_copy(args.binary, candidate)
    baseline_hash = readonly_copy(reference_binary, baseline)
    commands, assets = {}, {}
    for role, command, binary in (('target', args.engine, candidate),
            ('baseline', reference['engine'], baseline)):
        asset_dir = args.output/f'{role}-assets'
        asset_dir.mkdir()
        rewritten, asset = canonical.snapshot_engine_assets(command, asset_dir)
        commands[role] = shlex.join([str(binary), *shlex.split(rewritten)])
        assets[role] = {'directory': str(asset_dir), 'files': asset}
    args.rules_dir = rules_dir
    provenance = {'schema_version': 1, 'created_utc': datetime.now(timezone.utc).isoformat(),
        'mode': 'iterations' if args.iterations is not None else 'time',
        'iterations': args.iterations, 'movetime_ms': args.movetime,
        'external_search_watchdog_seconds': args.watchdog,
        'adjudication_watchdog_seconds': args.adjudication_watchdog,
        'max_actions': args.max_actions, 'jobs': args.jobs,
        'worker_scheduling': 'OS-scheduled, no affinity', 'seed': args.seed,
        'seed_policy': 'SHA256(seed:position_id) uint32; identical for roles and colour reversal',
        'positions': rows, 'suite_sha256': canonical.digest(args.suite/'manifest.json'),
        'candidate_engine': args.engine, 'commands_without_position_seed': commands,
        'candidate_binary_source': str(args.binary), 'rules_binding_source_dir': rules_source_dir,
        'candidate_binary_overrides_build': args.binary != canonical.engine_binary(args.build),
        'build_record_note': 'Build/cache/source record describes --build; an explicit --binary may come from another frozen build.',
        'candidate_sha256': candidate_hash, 'baseline_sha256': baseline_hash,
        'reference': reference, 'candidate_build': record, 'binding_sha256': binding_hashes,
        'assets': assets, 'runner_sha256': canonical.digest(__file__),
        'autoplay_sha256': canonical.digest(Path(autoplay.__file__)),
        'canonical_sha256': canonical.digest(Path(canonical.__file__)),
        'python': sys.version,
        'note': 'Nonterminal fixed-iteration mismatches invalidate the complete colour pair.'}
    save(args.output/'run.json', provenance)
    return rows, commands, provenance


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', required=True, help='candidate engine/options; binary supplied separately')
    parser.add_argument('--reference', type=Path, default=ROOT/'logs/canonical/reference-linear-study-20261105')
    parser.add_argument('--build', type=Path, default=ROOT/'build-canonical')
    parser.add_argument('--binary', type=Path, help='candidate executable override')
    parser.add_argument('--rules-dir', type=Path, help='rules binding override; defaults to --build')
    parser.add_argument('--suite', type=Path, default=ROOT/'test/engine_suite/v1')
    parser.add_argument('--subset', choices=canonical.SUBSET_SIZES, default='pool')
    parser.add_argument('--skip-first', type=int, default=0)
    parser.add_argument('--limit', type=int)
    budget = parser.add_mutually_exclusive_group(required=True)
    budget.add_argument('--iterations', type=int)
    budget.add_argument('--movetime', type=int, help='equal engine search time in milliseconds')
    parser.add_argument('--jobs', type=int, default=5)
    parser.add_argument('--seed', type=int, default=20261106)
    parser.add_argument('--max-actions', type=int, default=160)
    parser.add_argument('--watchdog', type=float, default=30, help='external per-search watchdog, seconds')
    parser.add_argument('--adjudication-watchdog', type=float, default=30)
    parser.add_argument('--protocol-timeout', type=int, default=5000, help='handshake/barrier timeout, milliseconds')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.binary = args.binary or canonical.engine_binary(args.build)
    args.rules_dir = args.rules_dir or args.build
    for key, value in vars(args).items():
        if isinstance(value, Path):
            setattr(args, key, value.resolve())
    if args.iterations is not None and (args.iterations <= 0 or args.iterations % 10):
        parser.error('--iterations must be a positive multiple of ten')
    if args.movetime is not None and args.movetime <= 0:
        parser.error('--movetime must be positive')
    if min(args.jobs, args.max_actions, args.watchdog, args.adjudication_watchdog,
            args.protocol_timeout) <= 0 or args.skip_first < 0 or (args.limit is not None and args.limit <= 0):
        parser.error('positive worker/budget limits and nonnegative --skip-first are required')
    if not math.isfinite(args.watchdog) or not math.isfinite(args.adjudication_watchdog):
        parser.error('watchdogs must be finite')
    if args.movetime is not None and args.watchdog <= args.movetime/1000:
        parser.error('external watchdog must exceed the requested search time')
    try:
        rows, commands, provenance = prepare(args)
        print(f"Mode={provenance['mode']}; pairs={len(rows)}; workers={args.jobs} OS-scheduled", flush=True)
        print(json.dumps(commands), flush=True)
        experiment = Experiment(args, rows, commands, autoplay.load_rules(args.rules_dir))
        code = 0
        try:
            asyncio.run(experiment.run())
        except KeyboardInterrupt:
            code = 130
        finally:
            for asset in provenance['assets'].values():
                canonical.verify_engine_assets(Path(asset['directory']), asset['files'])
            for name, expected in provenance['binding_sha256'].items():
                if canonical.digest(args.rules_dir/name) != expected:
                    raise ValueError('rules binding snapshot changed')
            for row in rows:
                if canonical.digest(args.output/'openings'/f"{row['id']}.5dpgn") != row['sha256']:
                    raise ValueError('opening snapshot changed')
            if canonical.digest(args.output/('candidate'+args.binary.suffix)) != provenance['candidate_sha256']:
                raise ValueError('candidate snapshot changed')
            ref_suffix = (args.reference/provenance['reference'].get('binary_name', '5dchess')).suffix
            if canonical.digest(args.output/('baseline'+ref_suffix)) != provenance['baseline_sha256']:
                raise ValueError('baseline snapshot changed')
            summary = experiment.summary()
            save(args.output/'summary.json', summary)
            if code == 0 and (summary['failure_pairs'] or summary['committed_prefix_pairs'] != len(rows)):
                code = 2
            provenance['exit_code'] = code
            save(args.output/'run.json', provenance)
            print(json.dumps(summary, indent=2), flush=True)
        return code
    except (OSError, ValueError, KeyError) as exc:
        parser.exit(2, f'Error: {exc}\n')


if __name__ == '__main__':
    raise SystemExit(main())

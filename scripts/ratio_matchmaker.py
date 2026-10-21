#!/usr/bin/env python3
"""Compare a target engine with baseline using position-scaled iteration budgets.

Baseline searches every position for a short fixed time.  On a target turn, a
separate baseline process first searches the *same* position and supplies the
reference iteration count.  The target then receives ``ratio * iterations``
(rounded to the engine's ten-iteration ``go depth`` unit) with a wall-time
safety cap derived from the measured target/baseline IPS ratio.

Workers continuously run color-reversed pairs sharing an opening and CPU.
Adaptive scheduling explores lower ratios early, then concentrates on
neighboring ratios around the noninferiority boundary. Each ratio retains
its own evidence, committed in pair-number order. The run-wide alpha budget
covers all ratios and three claims per ratio. Sequential scheduling and the
older fixed-look tests remain available. Ctrl-C cancels active games and
prints the smallest directly established ratio.
"""

from __future__ import annotations

import argparse
import asyncio
import csv
from dataclasses import dataclass, field
from datetime import datetime
import math
import os
from pathlib import Path
import statistics
import sys
from typing import Iterable

import autoplay
import cpu_affinity
import matchmaker_options as common
from elo import DefaultsHelpArgumentParser


DEPTH_TO_ITERATIONS = 10


@dataclass(frozen=True)
class SearchSample:
    iterations: int
    seconds: float

    @property
    def ips(self) -> float:
        return self.iterations / self.seconds if self.seconds > 0 else 0.0


@dataclass
class Throughput:
    target_iterations: int = 0
    target_seconds: float = 0.0
    baseline_iterations: int = 0
    baseline_seconds: float = 0.0
    paired_ratios: list[float] = field(default_factory=list)

    def add_target(self, sample: SearchSample) -> None:
        self.target_iterations += sample.iterations
        self.target_seconds += sample.seconds

    def add_baseline(self, sample: SearchSample) -> None:
        self.baseline_iterations += sample.iterations
        self.baseline_seconds += sample.seconds

    def add_pair(self, target: SearchSample, baseline: SearchSample) -> None:
        self.add_target(target)
        if target.ips > 0 and baseline.ips > 0:
            self.paired_ratios.append(target.ips / baseline.ips)

    @property
    def target_ips(self) -> float:
        return (
            self.target_iterations / self.target_seconds
            if self.target_seconds > 0 else 0.0
        )

    @property
    def baseline_ips(self) -> float:
        return self.baseline_iterations / self.baseline_seconds if self.baseline_seconds > 0 else 0.0

    @property
    def aggregate_ratio(self) -> float | None:
        if self.target_ips <= 0 or self.baseline_ips <= 0:
            return None
        return self.target_ips / self.baseline_ips

    @property
    def cap_ratio(self) -> float | None:
        """Robust target/baseline IPS ratio used to size the next safety cap."""

        if not self.paired_ratios:
            return self.aggregate_ratio
        return statistics.median(self.paired_ratios[-64:])

    @property
    def paired_geometric_ratio(self) -> float | None:
        positive = [value for value in self.paired_ratios if value > 0]
        if not positive:
            return None
        return math.exp(sum(math.log(value) for value in positive) / len(positive))


@dataclass
class GameResult:
    outcome: str  # win, draw, loss, or void from the target's perspective
    reason: str
    actions: int
    pgn: str


@dataclass
class RatioResult:
    ratio: float
    game_wins: int = 0
    game_draws: int = 0
    game_losses: int = 0
    void_games: int = 0
    pair_wins: int = 0
    pair_ties: int = 0
    pair_losses: int = 0
    void_pairs: int = 0
    pair_score_sum: float = 0.0
    decision: str = "running"
    superiority_p: float | None = None
    noninferiority_p: float | None = None
    inferiority_p: float | None = None
    pair_scores: list[float] = field(default_factory=list)
    superiority_log_e: float = 0.0
    noninferiority_log_e: float = 0.0
    inferiority_log_e: float = 0.0
    superiority_max_log_e: float = 0.0
    noninferiority_max_log_e: float = 0.0
    inferiority_max_log_e: float = 0.0

    @property
    def completed_games(self) -> int:
        return self.game_wins + self.game_draws + self.game_losses + self.void_games

    @property
    def decisive_pairs(self) -> int:
        return self.pair_wins + self.pair_losses

    @property
    def valid_pairs(self) -> int:
        return self.pair_wins + self.pair_ties + self.pair_losses

    @property
    def mean_pair_score(self) -> float | None:
        return self.pair_score_sum / self.valid_pairs if self.valid_pairs else None

    def add_game(self, outcome: str) -> None:
        if outcome == "win":
            self.game_wins += 1
        elif outcome == "draw":
            self.game_draws += 1
        elif outcome == "loss":
            self.game_losses += 1
        else:
            self.void_games += 1

    def add_pair(self, games: Iterable[GameResult]) -> None:
        games = list(games)
        if len(games) != 2 or any(game.outcome == "void" for game in games):
            self.void_pairs += 1
            return
        points = sum(
            1.0 if game.outcome == "win" else 0.5 if game.outcome == "draw" else 0.0
            for game in games
        )
        score = points / 2.0
        self.pair_score_sum += score
        self.pair_scores.append(score)
        if points > 1.0:
            self.pair_wins += 1
        elif points < 1.0:
            self.pair_losses += 1
        else:
            self.pair_ties += 1


@dataclass
class Experiment:
    args: argparse.Namespace
    rules: object
    openings: list[str]
    output_dir: Path
    throughput: Throughput = field(default_factory=Throughput)
    cpu_throughput: dict[int | None, Throughput] = field(default_factory=dict)
    results: list[RatioResult] = field(default_factory=list)
    interrupted: bool = False
    engine_seeds: common.EngineSeeds = field(default_factory=common.EngineSeeds)

    def engine(self, role: str, command: str, cpu: int | None, **context) -> autoplay.EngineProcess:
        command, seed = self.engine_seeds.prepare(command)
        common.record_engine_launch(self.output_dir, role=role, command=command,
                                    seed=seed, cpu=cpu, **context)
        return autoplay.EngineProcess(role, command, self.args.timeout, cpu=cpu)

    def throughput_on(self, cpu: int | None) -> Throughput:
        return self.cpu_throughput.setdefault(cpu, Throughput())

    def add_baseline(self, cpu: int | None, sample: SearchSample) -> None:
        self.throughput.add_baseline(sample)
        self.throughput_on(cpu).add_baseline(sample)

    def add_pair(self, cpu: int | None, target: SearchSample, baseline: SearchSample) -> None:
        self.throughput.add_pair(target, baseline)
        self.throughput_on(cpu).add_pair(target, baseline)

    @property
    def games_csv(self) -> Path:
        return self.output_dir / "games.csv"

    @property
    def searches_csv(self) -> Path:
        return self.output_dir / "searches.csv"

    def initialize_logs(self) -> None:
        self.output_dir.mkdir(parents=True, exist_ok=True)
        with self.games_csv.open("w", newline="") as output:
            csv.DictWriter(
                output,
                fieldnames=(
                    "ratio", "pair", "game", "target_color",
                    "outcome", "actions", "reason", "pgn_path", "cpu",
                ),
            ).writeheader()
        with self.searches_csv.open("w", newline="") as output:
            csv.DictWriter(
                output,
                fieldnames=(
                    "ratio", "pair", "game", "action", "role",
                    "reference_iterations", "reference_elapsed_seconds",
                    "reference_ips", "requested_iterations", "iteration_limit",
                    "cap_ips_ratio", "movetime_cap_ms", "actual_iterations",
                    "elapsed_seconds", "ips", "time_capped", "cpu",
                ),
            ).writeheader()

    def record_search(self, row: dict[str, object]) -> None:
        with self.searches_csv.open("a", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=row.keys())
            writer.writerow(row)
            output.flush()

    def record_game(
        self,
        ratio: float,
        pair_number: int,
        game_number: int,
        target_white: bool,
        result: GameResult,
        cpu: int | None = None,
    ) -> None:
        ratio_text = ratio_slug(ratio)
        pgn_path = self.output_dir / (
            f"ratio-{ratio_text}-pair-{pair_number:04d}-"
            f"{'target-white' if target_white else 'target-black'}.5dpgn"
        )
        pgn_path.write_text(result.pgn)
        with self.games_csv.open("a", newline="") as output:
            writer = csv.DictWriter(
                output,
                fieldnames=(
                    "ratio", "pair", "game", "target_color",
                    "outcome", "actions", "reason", "pgn_path", "cpu",
                ),
            )
            writer.writerow(
                {
                    "ratio": ratio,
                    "pair": pair_number,
                    "game": game_number,
                    "target_color": "white" if target_white else "black",
                    "outcome": result.outcome,
                    "actions": result.actions,
                    "reason": result.reason,
                    "pgn_path": pgn_path,
                    "cpu": cpu,
                }
            )
            output.flush()


def ratio_slug(value: float) -> str:
    return format(value, ".6g").replace(".", "p")


def search_sample(player: autoplay.EngineProcess) -> SearchSample:
    try:
        iterations = int(player.last_engine_stats["iterations"])
    except (KeyError, ValueError) as exc:
        raise autoplay.ProtocolError(
            f"{player.name}: search did not report a valid iteration count"
        ) from exc
    seconds_text = player.last_engine_stats.get("elapsed_seconds", "")
    try:
        seconds = float(seconds_text) if seconds_text else float(player.last_go_seconds)
    except (TypeError, ValueError) as exc:
        raise autoplay.ProtocolError(
            f"{player.name}: search did not report a valid duration"
        ) from exc
    if iterations <= 0 or seconds <= 0:
        raise autoplay.ProtocolError(
            f"{player.name}: unusable search metrics iterations={iterations}, seconds={seconds}"
        )
    return SearchSample(iterations, seconds)


def iteration_budget(reference_iterations: int, ratio: float) -> tuple[int, int]:
    requested = max(1, math.ceil(reference_iterations * ratio))
    depth = max(1, math.ceil(requested / DEPTH_TO_ITERATIONS))
    return requested, depth * DEPTH_TO_ITERATIONS


def target_time_cap_ms(
    baseline_time_ms: int,
    ips_ratio: float,
    cap_factor: float,
    absolute_cap_ms: int,
) -> int:
    if ips_ratio <= 0:
        return absolute_cap_ms
    predicted_cap = math.ceil(min(absolute_cap_ms, cap_factor * baseline_time_ms / ips_ratio))
    return min(absolute_cap_ms, max(baseline_time_ms, predicted_cap))


def binomial_upper_tail(successes: int, trials: int, probability: float) -> float:
    if trials <= 0:
        return 1.0
    return min(
        1.0,
        sum(
            math.comb(trials, value)
            * probability**value
            * (1.0 - probability) ** (trials - value)
            for value in range(successes, trials + 1)
        ),
    )


def binomial_lower_tail(successes: int, trials: int, probability: float) -> float:
    if trials <= 0:
        return 1.0
    return min(
        1.0,
        sum(
            math.comb(trials, value)
            * probability**value
            * (1.0 - probability) ** (trials - value)
            for value in range(0, successes + 1)
        ),
    )


def hoeffding_upper_p(mean: float, samples: int, null_mean: float) -> float:
    """Distribution-free one-sided p-value bound for samples in [0, 1]."""

    if samples <= 0 or mean <= null_mean:
        return 1.0
    return min(1.0, math.exp(-2.0 * samples * (mean - null_mean) ** 2))


def bounded_mean_log_e(
    samples: Iterable[float],
    null_mean: float,
    alternative_mean: float,
) -> float:
    """Return log evidence for an always-valid bounded-mean test.

    Samples must lie in [0, 1].  An alternative above the null tests the
    composite null E[X | past] <= null_mean; an alternative below it tests
    E[X | past] >= null_mean.  Every multiplicative factor has conditional
    expectation at most one under its null, making their product an e-process.
    """

    if not 0.0 < null_mean < 1.0:
        raise ValueError("null mean must be between zero and one")
    if not 0.0 < alternative_mean < 1.0 or alternative_mean == null_mean:
        raise ValueError("alternative mean must differ and lie between zero and one")
    bet = abs(alternative_mean - null_mean) / (
        null_mean * (1.0 - null_mean)
    )
    direction = 1.0 if alternative_mean > null_mean else -1.0
    log_e = 0.0
    for sample in samples:
        if not 0.0 <= sample <= 1.0:
            raise ValueError("bounded-mean samples must lie in [0, 1]")
        factor = 1.0 + bet * direction * (sample - null_mean)
        log_e += math.log(factor)
    return log_e


def anytime_p_from_log_e(max_log_e: float) -> float:
    """Return Ville's anytime-valid p-value from maximum evidence so far."""

    return min(1.0, math.exp(-max_log_e))


def update_anytime_evidence(
    result: RatioResult,
    noninferiority_margin: float,
    superiority_target: float,
) -> None:
    scores = result.pair_scores
    result.superiority_log_e = bounded_mean_log_e(
        scores, 0.5, superiority_target
    )
    boundary = 0.5 - noninferiority_margin
    result.inferiority_log_e = bounded_mean_log_e(
        scores, boundary, max(boundary / 2, boundary - (superiority_target - 0.5))
    )
    result.noninferiority_log_e = bounded_mean_log_e(
        scores, 0.5 - noninferiority_margin, 0.5
    )
    result.superiority_max_log_e = max(
        result.superiority_max_log_e, result.superiority_log_e
    )
    result.noninferiority_max_log_e = max(
        result.noninferiority_max_log_e, result.noninferiority_log_e
    )
    result.inferiority_max_log_e = max(
        result.inferiority_max_log_e, result.inferiority_log_e
    )
    result.superiority_p = anytime_p_from_log_e(result.superiority_max_log_e)
    result.noninferiority_p = anytime_p_from_log_e(
        result.noninferiority_max_log_e
    )
    result.inferiority_p = anytime_p_from_log_e(result.inferiority_max_log_e)


def evaluate_anytime_ratio(
    result: RatioResult,
    alpha: float,
    noninferiority_margin: float,
    superiority_target: float,
) -> str | None:
    update_anytime_evidence(result, noninferiority_margin, superiority_target)
    boundary = math.log(1.0 / alpha)
    if (
        result.superiority_max_log_e >= boundary
        and result.noninferiority_max_log_e >= boundary
    ):
        return "better"
    if result.noninferiority_max_log_e >= boundary:
        return "noninferior"
    if result.inferiority_max_log_e >= boundary:
        return "inferior"
    return None


def evaluate_ratio(
    result: RatioResult,
    alpha_at_look: float,
    noninferiority_margin: float,
) -> str | None:
    trials = result.decisive_pairs
    wins = result.pair_wins
    result.superiority_p = binomial_upper_tail(wins, trials, 0.5)
    result.noninferiority_p = hoeffding_upper_p(
        result.mean_pair_score or 0.0,
        result.valid_pairs,
        0.5 - noninferiority_margin,
    )
    result.inferiority_p = binomial_lower_tail(wins, trials, 0.5)
    if (
        result.superiority_p <= alpha_at_look
        and result.noninferiority_p <= alpha_at_look
    ):
        return "better"
    if result.noninferiority_p <= alpha_at_look:
        return "noninferior"
    if result.inferiority_p <= alpha_at_look:
        return "inferior"
    return None


def format_probability(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.4g}"


def format_ratio(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.3f}"


def format_evidence(log_e: float) -> str:
    if log_e > math.log(sys.float_info.max):
        return f"exp({log_e:.2f})"
    return f"{math.exp(log_e):.4g}"


def print_live(
    result: RatioResult,
    throughput: Throughput,
    statistical_method: str,
    pair_complete: bool = False,
) -> None:
    line = (
        f"[m={result.ratio:g}] games {result.completed_games}: "
        f"target {result.game_wins}-{result.game_draws}-{result.game_losses} "
        f"(void {result.void_games}); pairs "
        f"{result.pair_wins}-{result.pair_ties}-{result.pair_losses} "
        f"(void {result.void_pairs}, mean score "
        f"{format_ratio(result.mean_pair_score)}); target/baseline IPS "
        f"{format_ratio(throughput.aggregate_ratio)}"
    )
    if statistical_method == "anytime" and pair_complete and result.valid_pairs:
        line += (
            "; current E superior/acceptable/unacceptable "
            f"{format_evidence(result.superiority_log_e)}/"
            f"{format_evidence(result.noninferiority_log_e)}/"
            f"{format_evidence(result.inferiority_log_e)}; anytime p "
            f"{format_probability(result.superiority_p)}/"
            f"{format_probability(result.noninferiority_p)}/"
            f"{format_probability(result.inferiority_p)}"
        )
    print(line, flush=True)


async def start_engines(players: Iterable[autoplay.EngineProcess]) -> None:
    players = list(players)
    results = await asyncio.gather(
        *(player.start() for player in players), return_exceptions=True
    )
    for player, result in zip(players, results):
        if isinstance(result, Exception):
            raise autoplay.ProtocolError(f"{player.name}: startup failed: {result}")
    results = await asyncio.gather(
        *(player.new_game() for player in players), return_exceptions=True
    )
    for player, result in zip(players, results):
        if isinstance(result, Exception):
            raise autoplay.ProtocolError(f"{player.name}: new-game failed: {result}")


async def calibrate_on(experiment: Experiment, cpu: int | None) -> None:
    args = experiment.args
    baseline = experiment.engine("baseline", args.baseline, cpu, phase="calibration")
    target = experiment.engine("target", args.target, cpu, phase="calibration")
    commands: asyncio.Queue[str] = asyncio.Queue()
    game = experiment.rules.game.from_pgn(experiment.openings[0])
    position = autoplay.uci_position(game)
    try:
        await start_engines((baseline, target))
        await baseline.choose(position, [], args.baseline_movetime, commands)
        baseline_sample = search_sample(baseline)
        experiment.add_baseline(cpu, baseline_sample)
        await target.choose(position, [], args.baseline_movetime, commands)
        target_sample = search_sample(target)
        experiment.add_pair(cpu, target_sample, baseline_sample)
    finally:
        await asyncio.gather(baseline.close(), target.close())
    print(
        f"Calibration CPU {cpu if cpu is not None else 'unpinned'}: "
        f"baseline={baseline_sample.ips:.0f} IPS, target={target_sample.ips:.0f} IPS, "
        f"target/baseline={target_sample.ips / baseline_sample.ips:.3f}",
        flush=True,
    )


def winner_to_target_outcome(winner: str, target_white: bool) -> str:
    if winner == "draw":
        return "draw"
    target_won = (winner == "white") == target_white
    return "win" if target_won else "loss"


async def play_game(
    experiment: Experiment,
    ratio: float,
    pair_number: int,
    game_number: int,
    opening: str,
    target_white: bool,
    cpu: int | None = None,
) -> GameResult:
    args = experiment.args
    rules = experiment.rules
    context = dict(phase="game", ratio=ratio, pair=pair_number, game=game_number)
    target = experiment.engine("target", args.target, cpu, **context)
    baseline = experiment.engine("baseline", args.baseline, cpu, **context)
    meter = experiment.engine("baseline meter", args.baseline, cpu, **context)
    players = [target, baseline] if target_white else [baseline, target]
    commands: asyncio.Queue[str] = asyncio.Queue()
    game = rules.game.from_pgn(opening)
    metadata = dict(game.metadata)
    metadata.update(
        {
            "event": args.event,
            "site": "Local",
            "date": datetime.now().strftime("%Y.%m.%d"),
            "round": f"{ratio:g}.{pair_number}.{1 if target_white else 2}",
            "white": "Target" if target_white else "Baseline",
            "black": "Baseline" if target_white else "Target",
            "result": "*",
        }
    )
    game.metadata = metadata
    initial_position = autoplay.uci_position(game)
    history: list[str] = []
    show_flags = rules.SHOW_CAPTURE | rules.SHOW_PROMOTION | rules.SHOW_SHORT
    outcome = "void"
    reason = "game did not start"
    actions = 0
    try:
        await start_engines((target, baseline, meter))
        for action_number in range(1, args.max_actions + 1):
            actions = action_number
            _, black_to_move = game.get_current_present()
            color_index = int(black_to_move)
            player = players[color_index]
            target_turn = player is target
            if target_turn:
                try:
                    meter_moves = await meter.choose(
                        initial_position, history, args.baseline_movetime, commands
                    )
                except (autoplay.ProtocolError, ValueError) as exc:
                    outcome = "void"
                    reason = f"baseline meter failure: {exc}"
                    break
                if not meter_moves:
                    status = await autoplay.adjudicate(
                        autoplay.snapshot_pgn(game), args.module_dir
                    )
                    if status in ("white", "black", "draw"):
                        outcome = winner_to_target_outcome(status, target_white)
                        reason = f"baseline meter detected terminal position: {status}"
                    else:
                        outcome = "void"
                        reason = (
                            "baseline meter returned no move in a nonterminal "
                            f"position ({status})"
                        )
                    break
                reference = search_sample(meter)
                experiment.add_baseline(cpu, reference)
                requested, iteration_limit = iteration_budget(
                    reference.iterations, ratio
                )
                current_ips_ratio = experiment.throughput_on(cpu).cap_ratio or 1.0
                cap_ms = target_time_cap_ms(
                    args.baseline_movetime,
                    current_ips_ratio,
                    args.cap_factor,
                    args.max_target_movetime,
                )
                depth = iteration_limit // DEPTH_TO_ITERATIONS
                try:
                    moves = await target.choose(
                        initial_position, history, cap_ms, commands, depth=depth
                    )
                except (autoplay.ProtocolError, ValueError) as exc:
                    outcome = "loss"
                    reason = f"target failure: {exc}"
                    break
                if moves:
                    sample = search_sample(target)
                    experiment.add_pair(cpu, sample, reference)
                    experiment.record_search(
                        {
                            "ratio": ratio,
                            "pair": pair_number,
                            "game": game_number,
                            "action": action_number,
                            "role": "target",
                            "reference_iterations": reference.iterations,
                            "reference_elapsed_seconds": reference.seconds,
                            "reference_ips": reference.ips,
                            "requested_iterations": requested,
                            "iteration_limit": iteration_limit,
                            "cap_ips_ratio": current_ips_ratio,
                            "movetime_cap_ms": cap_ms,
                            "actual_iterations": sample.iterations,
                            "elapsed_seconds": sample.seconds,
                            "ips": sample.ips,
                            "time_capped": int(sample.iterations < iteration_limit),
                            "cpu": cpu,
                        }
                    )
            else:
                try:
                    moves = await baseline.choose(
                        initial_position, history, args.baseline_movetime, commands
                    )
                except (autoplay.ProtocolError, ValueError) as exc:
                    outcome = "win"
                    reason = f"baseline failure: {exc}"
                    break
                if moves:
                    sample = search_sample(baseline)
                    experiment.add_baseline(cpu, sample)
                    experiment.record_search(
                        {
                            "ratio": ratio,
                            "pair": pair_number,
                            "game": game_number,
                            "action": action_number,
                            "role": "baseline",
                            "reference_iterations": "",
                            "reference_elapsed_seconds": "",
                            "reference_ips": "",
                            "requested_iterations": "",
                            "iteration_limit": "",
                            "cap_ips_ratio": "",
                            "movetime_cap_ms": args.baseline_movetime,
                            "actual_iterations": sample.iterations,
                            "elapsed_seconds": sample.seconds,
                            "ips": sample.ips,
                            "time_capped": "",
                            "cpu": cpu,
                        }
                    )

            if not moves:
                status = await autoplay.adjudicate(
                    autoplay.snapshot_pgn(game), args.module_dir
                )
                if status in ("white", "black", "draw"):
                    outcome = winner_to_target_outcome(status, target_white)
                    reason = f"terminal position: {status}"
                elif target_turn:
                    outcome = "loss"
                    reason = f"target returned no move in nonterminal position ({status})"
                else:
                    outcome = "win"
                    reason = f"baseline returned no move in nonterminal position ({status})"
                break
            error = autoplay.apply_action(rules, game, moves)
            if error:
                if target_turn:
                    outcome = "loss"
                    reason = f"target illegal action: {error}"
                else:
                    outcome = "win"
                    reason = f"baseline illegal action: {error}"
                break
            history.extend(moves)
            history.append("submit")
        else:
            outcome = "draw"
            reason = "action limit reached"
        autoplay.set_pgn_result(
            game,
            "draw" if outcome == "draw" else
            "white" if (outcome == "win") == target_white else
            "black" if outcome in ("win", "loss") else "protocol",
        )
        autoplay.append_termination_comment(game, reason)
        return GameResult(outcome, reason, actions, game.show_pgn(show_flags))
    except asyncio.CancelledError:
        raise
    except Exception as exc:
        reason = f"controller error: {type(exc).__name__}: {exc}"
        autoplay.append_termination_comment(game, reason)
        return GameResult("void", reason, actions, game.show_pgn(show_flags))
    finally:
        await asyncio.gather(target.close(), baseline.close(), meter.close())


async def play_pair(
    experiment: Experiment,
    result: RatioResult,
    pair_number: int,
    cpu: int | None,
) -> list[GameResult]:
    args = experiment.args
    opening = experiment.openings[(pair_number - 1) % len(experiment.openings)]

    async def one(target_white: bool) -> GameResult:
        game_number = (pair_number - 1) * 2 + (1 if target_white else 2)
        game = await play_game(
            experiment, result.ratio, pair_number, game_number,
            opening, target_white, cpu,
        )
        result.add_game(game.outcome)
        experiment.record_game(
            result.ratio, pair_number, game_number, target_white, game, cpu
        )
        print_live(result, experiment.throughput, args.stat_method)
        return game

    # A slot owns one CPU. Run both colors on it, with only one search active
    # at a time, then immediately reuse the slot for another pair.
    return [await one(True), await one(False)]


@dataclass
class RatioState:
    result: RatioResult
    next_pair: int = 1
    next_commit: int = 1
    active: int = 0
    completed: dict[int, list[GameResult]] = field(default_factory=dict)
    next_look: int = 0

    def passed(self, args: argparse.Namespace) -> bool:
        return self.result.decision == "better" or (
            self.result.decision == "noninferior" and args.advance_on == "noninferior"
        )

    def can_launch(self, args: argparse.Namespace) -> bool:
        return (self.next_pair <= args.max_pairs and not self.passed(args)
                and self.result.decision not in ("inferior", "conflicting"))

    def exhausted(self, args: argparse.Namespace) -> bool:
        return not self.can_launch(args) and self.active == 0

    def commit(self, experiment: Experiment) -> None:
        args = experiment.args
        result = self.result
        while self.next_commit in self.completed:
            games = self.completed.pop(self.next_commit)
            self.next_commit += 1
            result.add_pair(games)
            previous = result.decision
            if args.stat_method == "anytime":
                decision = evaluate_anytime_ratio(
                    result, args.test_alpha, args.noninferiority_margin,
                    args.superiority_target,
                )
                # Historical crossings remain claims even if current evidence
                # falls. Surface incompatible crossings rather than hide one.
                boundary = math.log(1 / args.test_alpha)
                if (result.noninferiority_max_log_e >= boundary
                        and result.inferiority_max_log_e >= boundary):
                    decision = "conflicting"
                if decision is not None:
                    result.decision = decision
            else:
                looks = args.look_pairs
                if self.next_look < len(looks) and result.valid_pairs >= looks[self.next_look]:
                    alpha = args.test_alpha * looks[self.next_look] / sum(looks)
                    decision = evaluate_ratio(result, alpha, args.noninferiority_margin)
                    self.next_look += 1
                    if decision is not None and (previous != "better" or decision == "better"):
                        result.decision = decision
                    print(f"Fixed look: m={result.ratio:g}, pairs={result.valid_pairs}, "
                          f"per-claim alpha={alpha:.4g}", flush=True)
            print_live(result, experiment.throughput, args.stat_method, pair_complete=True)
            if result.decision != previous:
                print(f"m={result.ratio:g} decision: {result.decision}", flush=True)


class SequentialPolicy:
    def __init__(self, states: list[RatioState], args: argparse.Namespace):
        self.states, self.args = states, args
        self.focus = 0
        self.finished = False

    def choose(self) -> int | None:
        while self.focus < len(self.states):
            state = self.states[self.focus]
            if state.passed(self.args):
                self.focus += 1
                continue
            if state.result.decision in ("inferior", "conflicting") or state.exhausted(self.args):
                self.finished = True
                return None
            return self.focus if state.can_launch(self.args) else None
        self.finished = True
        return None


class AdaptivePolicy:
    """Optimistic descent followed by persistent two-ratio refinement.

    Provisional signals only allocate work. No evidence is transferred between
    ratios, reset on revisits, or manufactured from a monotonicity assumption.
    """

    def __init__(self, states: list[RatioState], args: argparse.Namespace):
        self.states, self.args = states, args
        self.focus = 0
        self.bracket: tuple[int, int] | None = None
        self.allocations = 0
        self.finished = False

    def _refine(self, lower: int) -> None:
        self.bracket = (lower - 1, lower)
        self.allocations = 0
        high, low = (self.states[i].result.ratio for i in self.bracket)
        print(f"Adaptive refinement: m={high:g} / m={low:g}; "
              "75% of new pairs favor the higher ratio until it is certified.", flush=True)

    def choose(self) -> int | None:
        args = self.args
        # Several already-resolved ratios may be traversed on one call.
        while not self.finished:
            if any(state.result.decision == "conflicting" for state in self.states):
                self.finished = True
                return None
            if self.bracket is None:
                state = self.states[self.focus]
                result = state.result
                if state.passed(args):
                    if self.focus == len(self.states) - 1:
                        self.finished = True
                        return None
                    self.focus += 1
                    print(f"Adaptive descent: new pairs at m={self.states[self.focus].result.ratio:g}",
                          flush=True)
                    continue
                if result.decision == "inferior":
                    if self.focus == 0:
                        self.finished = True
                        return None
                    self._refine(self.focus)
                    continue
                if result.valid_pairs >= args.switch_min_pairs:
                    if self.focus > 0 and result.inferiority_log_e >= math.log(args.switch_e):
                        self._refine(self.focus)
                        continue
                    evidence = (result.superiority_log_e if args.advance_on == "better"
                                else result.noninferiority_log_e)
                    if self.focus < len(self.states) - 1 and evidence >= math.log(args.switch_e):
                        self.focus += 1
                        print(f"Provisional descent: new pairs at m={self.states[self.focus].result.ratio:g} "
                              f"(current E >= {args.switch_e:g}; not certification).", flush=True)
                        continue
                if state.exhausted(args):
                    if self.focus == 0:
                        self.finished = True
                        return None
                    self._refine(self.focus)
                    continue
                return self.focus if state.can_launch(args) else None

            high, low = self.bracket
            upper, lower = self.states[high], self.states[low]
            if lower.passed(args):
                if low == len(self.states) - 1:
                    self.finished = True
                    return None
                self.focus, self.bracket = low + 1, None
                print(f"Adaptive descent: new pairs at m={self.states[self.focus].result.ratio:g}",
                      flush=True)
                continue
            if upper.result.decision == "inferior":
                if high == 0:
                    self.finished = True
                    return None
                self._refine(high)
                continue
            if upper.passed(args) and lower.result.decision == "inferior":
                self.finished = True
                return None
            candidates = [i for i in self.bracket if self.states[i].can_launch(args)]
            if not candidates:
                self.finished = all(self.states[i].active == 0 for i in self.bracket)
                return None
            preferred = low if self.allocations % 4 == 3 else high
            self.allocations += 1
            return preferred if preferred in candidates else candidates[0]
        return None


async def run_matches(experiment: Experiment) -> None:
    """Refill CPU slots independently of ordered statistical commitment."""
    args = experiment.args
    states = [RatioState(RatioResult(ratio, decision="not tested")) for ratio in args.ratios]
    experiment.results.extend(state.result for state in states)
    policy = (AdaptivePolicy if args.schedule == "adaptive" else SequentialPolicy)(states, args)
    free_slots = list(range(args.jobs))
    active: dict[asyncio.Task, tuple[int, int, int]] = {}
    try:
        while True:
            # Pair allocation order is fixed before execution. Completion speed
            # cannot choose which scores enter a ratio's evidence first.
            while free_slots:
                index = policy.choose()
                if index is None:
                    break
                slot = free_slots.pop(0)
                state = states[index]
                pair = state.next_pair
                state.next_pair += 1
                state.active += 1
                if state.result.decision == "not tested":
                    state.result.decision = "running"
                    print(f"Starting m={state.result.ratio:g}", flush=True)
                task = asyncio.create_task(play_pair(
                    experiment, state.result, pair, args.worker_cores[slot],
                ))
                active[task] = (slot, index, pair)
            # A final decision cancels the entire outstanding suffix, rather
            # than selecting which incomplete or out-of-order pairs to keep.
            if policy.finished or not active:
                break
            done, _ = await asyncio.wait(active, return_when=asyncio.FIRST_COMPLETED)
            for task in sorted(done, key=lambda item: active[item][1:]):
                slot, index, pair = active.pop(task)
                state = states[index]
                state.active -= 1
                free_slots.append(slot)
                state.completed[pair] = task.result()
                state.commit(experiment)
    finally:
        for task in active:
            task.cancel()
        await asyncio.gather(*active, return_exceptions=True)
        for state in states:
            if state.result.decision == "running":
                state.result.decision = "inconclusive"
            if state.completed:
                print(f"m={state.result.ratio:g}: {len(state.completed)} completed pairs "
                      "outside the committed prefix excluded from inference.", flush=True)


def print_final_summary(experiment: Experiment) -> None:
    print("\n=== Iteration-ratio summary ===")
    for result in experiment.results:
        if result.valid_pairs and experiment.args.stat_method == "anytime":
            update_anytime_evidence(
                result,
                experiment.args.noninferiority_margin,
                experiment.args.superiority_target,
            )
        p_name = "anytime p" if experiment.args.stat_method == "anytime" else "p"
        line = (
            f"m={result.ratio:g}: {result.decision}; games "
            f"{result.game_wins}-{result.game_draws}-{result.game_losses} "
            f"(void {result.void_games}); pairs "
            f"{result.pair_wins}-{result.pair_ties}-{result.pair_losses}, "
            f"mean score={format_ratio(result.mean_pair_score)}; "
            f"{p_name}(superior)={format_probability(result.superiority_p)}, "
            f"{p_name}(noninferior)={format_probability(result.noninferiority_p)}, "
            f"{p_name}(inferior)={format_probability(result.inferiority_p)}"
        )
        if experiment.args.stat_method == "anytime" and result.valid_pairs:
            line += (
                "; max e(superior/noninferior/inferior)="
                f"{format_evidence(result.superiority_max_log_e)}/"
                f"{format_evidence(result.noninferiority_max_log_e)}/"
                f"{format_evidence(result.inferiority_max_log_e)}"
            )
        print(line)
    established = [
        result.ratio
        for result in experiment.results
        if result.decision in ("better", "noninferior")
    ]
    if established:
        print(f"Most aggressive established ratio: m={min(established):g}")
        best = min(established)
        lower = next((result for result in experiment.results if result.ratio < best), None)
        if lower is not None:
            print(f"Next lower ratio: m={lower.ratio:g}, {lower.decision}")
    else:
        print("Most aggressive established ratio: none")
    print(
        f"Actual aggregate target IPS: {experiment.throughput.target_ips:.1f}\n"
        f"Actual aggregate baseline IPS:   {experiment.throughput.baseline_ips:.1f}\n"
        f"Actual target/baseline IPS ratio: "
        f"{format_ratio(experiment.throughput.aggregate_ratio)}\n"
        f"Paired geometric IPS ratio:   "
        f"{format_ratio(experiment.throughput.paired_geometric_ratio)}"
    )
    if experiment.interrupted:
        print("Run interrupted; unfinished games were excluded.")
    print(f"Logs: {experiment.output_dir.resolve()}")


def print_header(args: argparse.Namespace) -> None:
    print("=== Target / baseline iteration-ratio match ===", flush=True)
    print(f"Target:   {args.target}\nBaseline: {args.baseline}", flush=True)
    print(
        "Output guide:\n"
        "  m = target iterations / baseline-meter iterations on the same position; "
        "rounded up to 10 iterations.\n"
        "  games N: finished games; target W-D-L = wins-draws-losses.\n"
        "  pairs W-T-L: committed color-reversed pairs above/equal/below score 0.5.\n"
        "  mean score = target points / 2 per pair (win=1, draw=0.5, loss=0); "
        "void pairs are excluded.\n"
        "  IPS = iterations per second; target/baseline IPS is aggregate throughput, "
        "not the iteration budget ratio.\n"
        "  Game counts may lead pair counts while earlier pairs are still running.",
        flush=True,
    )
    if args.stat_method == "anytime":
        boundary = 0.5 - args.noninferiority_margin
        print(
            f"  Current E superior/acceptable/unacceptable: evidence for mean score "
            f"> 0.5 / > {boundary:g} / < {boundary:g}.\n"
            "  E starts at 1; larger favors that claim. Anytime p = min(1, 1 / max E); "
            "it is not the probability the claim is false.\n"
            f"  Formal threshold: max E >= {1 / args.test_alpha:g}; run-wide alpha "
            f"{args.alpha:g}, divided across {len(args.ratios)} ratios and 3 claims each.",
            flush=True,
        )
    else:
        print("  Legacy fixed looks: inferiority means below score 0.5; "
              "per-claim alpha is spent only at the declared pair counts.", flush=True)
    placement = ("on the same CPU" if args.worker_cores[0] is not None
                 else "without CPU affinity")
    print(f"Schedule: {args.schedule}; up to {args.jobs} concurrent games; "
          f"both colors of each pair run sequentially {placement}.", flush=True)
    if args.schedule == "adaptive":
        print(f"Provisional switching: current E >= {args.switch_e:g}, at least "
              f"{args.switch_min_pairs} valid pairs; evidence persists across visits.", flush=True)
    cpus = ",".join(map(str, args.worker_cores)) if args.worker_cores[0] is not None else "unpinned"
    controller = ",".join(map(str, args.controller_cpus)) if args.controller_cpus else "shared OS scheduling"
    print(f"Engine CPUs: {cpus}; controller/arbiter CPUs: {controller}", flush=True)
    print(f"Baseline search={args.baseline_movetime} ms; target safety cap <= "
          f"{args.max_target_movetime} ms; maximum {args.max_pairs} pairs per ratio.\n", flush=True)
    print("Engine RNG: fresh engine defaults; {seed} templates receive unique process seeds. "
          "Assignments are logged in engine-launches.jsonl.\n", flush=True)


configure_cpus = common.configure_cpus


async def run_experiment(experiment: Experiment) -> int:
    experiment.initialize_logs()
    print_header(experiment.args)
    try:
        # Calibration searches use the same CPUs as subsequent game searches.
        tasks = [asyncio.create_task(calibrate_on(experiment, cpu))
                 for cpu in dict.fromkeys(experiment.args.worker_cores)]
        try:
            await asyncio.gather(*tasks)
        finally:
            for task in tasks:
                if not task.done():
                    task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
        await run_matches(experiment)
        return 2 if any(result.decision == "conflicting" for result in experiment.results) else 0
    except asyncio.CancelledError:
        experiment.interrupted = True
        raise
    finally:
        print_final_summary(experiment)


def parse_float_list(text: str) -> list[float]:
    try:
        values = [float(item.strip()) for item in text.split(",") if item.strip()]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected a comma-separated number list") from exc
    if not values or any(not math.isfinite(value) or value <= 0 for value in values):
        raise argparse.ArgumentTypeError("all ratios must be finite and positive")
    if values != sorted(values, reverse=True) or len(values) != len(set(values)):
        raise argparse.ArgumentTypeError("ratios must be unique and descending")
    return values


def parse_int_list(text: str) -> list[int]:
    try:
        values = [int(item.strip()) for item in text.split(",") if item.strip()]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected a comma-separated integer list") from exc
    if not values or any(value <= 0 for value in values):
        raise argparse.ArgumentTypeError("all looks must be positive")
    if values != sorted(values) or len(values) != len(set(values)):
        raise argparse.ArgumentTypeError("looks must be unique and ascending")
    return values


def build_parser() -> argparse.ArgumentParser:
    parser = DefaultsHelpArgumentParser(description=__doc__)
    parser.add_argument(
        "--target",
        required=True,
        help="target engine command; optional {seed} is assigned uniquely to each engine process",
    )
    parser.add_argument(
        "--baseline",
        default="./build/5dchess zero",
        help="baseline command; optional {seed} is assigned uniquely to each engine process",
    )
    parser.add_argument(
        "--ratios", type=parse_float_list, default=parse_float_list("1,.75,.5,.25")
    )
    parser.add_argument("--baseline-movetime", type=int, default=400,
                        help="milliseconds per baseline or meter search")
    parser.add_argument(
        "--cap-factor",
        type=float,
        default=2.0,
        help="target wall cap = factor * baseline time / measured target/baseline IPS",
    )
    parser.add_argument("--max-target-movetime", type=int, default=10_000)
    parser.add_argument("--timeout", type=int, default=2_000,
                        help="extra protocol timeout in milliseconds")
    common.add_execution_options(parser)
    parser.add_argument("--schedule", choices=("adaptive", "sequential"), default=None,
                        help="scheduling policy (default: adaptive with anytime tests, sequential with fixed looks)")
    parser.add_argument("--switch-e", type=float, default=3.0,
                        help="current evidence needed for a provisional adaptive switch")
    parser.add_argument("--switch-min-pairs", type=int, default=8,
                        help="valid pairs needed before a provisional adaptive switch")
    parser.add_argument("--max-actions", type=int, default=500)
    parser.add_argument("--module-dir", type=Path, default=autoplay.DEFAULT_MODULE_DIR)
    opening_group = parser.add_mutually_exclusive_group()
    opening_group.add_argument("--opening", "--game", "-g", action="append", type=Path,
                               default=[], help="starting PGN file; repeat for an opening cycle")
    opening_group.add_argument("--opening-text", "-m", help="starting PGN text")
    parser.add_argument("--event", default="Iteration-ratio policy test", help="PGN event name")
    parser.add_argument(
        "--stat-method",
        choices=("anytime", "fixed-looks"),
        default="anytime",
        help="always-valid per-pair evidence or legacy planned looks",
    )
    parser.add_argument(
        "--advance-on",
        choices=("noninferior", "better"),
        default="noninferior",
        help="advance to the next ratio after this strength claim",
    )
    parser.add_argument(
        "--look-pairs",
        type=parse_int_list,
        default=parse_int_list("8,16,32,64,128,256"),
        help="predeclared valid-pair checkpoints for --stat-method fixed-looks",
    )
    parser.add_argument("--max-pairs", type=int, default=256)
    parser.add_argument("--alpha", type=float, default=0.05,
                        help="run-wide false-claim budget, divided among ratios and 3 claims each")
    parser.add_argument(
        "--superiority-target",
        type=float,
        default=0.60,
        help="alternative mean used by the always-valid superiority bet",
    )
    parser.add_argument(
        "--noninferiority-margin",
        type=float,
        default=0.10,
        help="largest tolerated deficit in mean normalized pair score",
    )
    common.add_output_options(parser, "logs/ratio-runs")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.baseline_movetime <= 0 or args.max_target_movetime <= 0 or args.timeout <= 0:
        parser.error("time limits must be positive")
    if not math.isfinite(args.cap_factor) or args.cap_factor <= 0 or args.jobs <= 0 or args.max_actions <= 0:
        parser.error("cap factor, jobs, and max actions must be positive and finite")
    if args.max_pairs <= 0 or args.switch_min_pairs <= 0:
        parser.error("--max-pairs and --switch-min-pairs must be positive")
    if not math.isfinite(args.switch_e) or args.switch_e <= 1:
        parser.error("--switch-e must be finite and greater than one")
    if not 0 < args.alpha < 1:
        parser.error("--alpha must be between zero and one")
    if not 0.5 < args.superiority_target < 1:
        parser.error("--superiority-target must be between 0.5 and one")
    if not 0 < args.noninferiority_margin < 0.5:
        parser.error("--noninferiority-margin must be between zero and 0.5")
    if args.stat_method == "fixed-looks" and args.max_pairs < args.look_pairs[-1]:
        parser.error("--max-pairs must be at least the final --look-pairs value")
    args.schedule = args.schedule or ("adaptive" if args.stat_method == "anytime" else "sequential")
    if args.stat_method == "fixed-looks" and args.schedule == "adaptive":
        parser.error("adaptive scheduling requires --stat-method anytime")
    args.test_alpha = args.alpha / (3 * len(args.ratios))
    configure_cpus(parser, args)
    try:
        common.validate_engine_command(args.target)
        common.validate_engine_command(args.baseline)
    except ValueError as exc:
        parser.error(str(exc))
    openings = []
    if args.opening:
        for path in args.opening:
            try:
                openings.append(path.read_text())
            except OSError as exc:
                parser.error(f"cannot read opening {path}: {exc}")
    else:
        openings.append(args.opening_text or '[Board "Standard - Turn Zero"]')
    try:
        args.output_dir = common.create_run_directory(args)
    except OSError as exc:
        parser.error(f"cannot create run directory: {exc}")
    try:
        if args.controller_cpus:
            cpu_affinity.pin_process(os.getpid(), set(args.controller_cpus))
        rules = autoplay.load_rules(args.module_dir)
        experiment = Experiment(args, rules, openings, args.output_dir)
        return asyncio.run(run_experiment(experiment))
    except KeyboardInterrupt:
        # asyncio cancellation runs run_experiment's finally block first.
        return 130
    except (autoplay.ProtocolError, OSError) as exc:
        print(f"Ratio matcher failed: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

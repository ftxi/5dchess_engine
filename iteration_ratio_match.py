#!/usr/bin/env python3
"""Compare a target engine with zero using position-scaled iteration budgets.

Zero searches every position for a short fixed time.  On a target turn, a
separate zero process first searches the *same* position and supplies the
reference iteration count.  The target then receives ``ratio * iterations``
(rounded to the engine's ten-iteration ``go depth`` unit) with a wall-time
safety cap derived from the measured target/zero IPS ratio.

Ratios are tested from largest to smallest.  Each statistical sample is a
color-reversed pair sharing an opening and seed.  The sweep advances after a
sequentially corrected superiority or non-inferiority test and stops on
significant inferiority or an inconclusive maximum sample.  Ctrl-C cancels
active games and still prints the strongest (smallest) established ratio.
"""

from __future__ import annotations

import argparse
import asyncio
import csv
from dataclasses import dataclass, field
from datetime import datetime
import math
from pathlib import Path
import statistics
import sys
from typing import Iterable

import autoplay


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
    zero_iterations: int = 0
    zero_seconds: float = 0.0
    paired_ratios: list[float] = field(default_factory=list)

    def add_target(self, sample: SearchSample) -> None:
        self.target_iterations += sample.iterations
        self.target_seconds += sample.seconds

    def add_zero(self, sample: SearchSample) -> None:
        self.zero_iterations += sample.iterations
        self.zero_seconds += sample.seconds

    def add_pair(self, target: SearchSample, zero: SearchSample) -> None:
        self.add_target(target)
        if target.ips > 0 and zero.ips > 0:
            self.paired_ratios.append(target.ips / zero.ips)

    @property
    def target_ips(self) -> float:
        return (
            self.target_iterations / self.target_seconds
            if self.target_seconds > 0 else 0.0
        )

    @property
    def zero_ips(self) -> float:
        return self.zero_iterations / self.zero_seconds if self.zero_seconds > 0 else 0.0

    @property
    def aggregate_ratio(self) -> float | None:
        if self.target_ips <= 0 or self.zero_ips <= 0:
            return None
        return self.target_ips / self.zero_ips

    @property
    def cap_ratio(self) -> float | None:
        """Robust target/zero IPS ratio used to size the next safety cap."""

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
        self.pair_score_sum += points / 2.0
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
    results: list[RatioResult] = field(default_factory=list)
    interrupted: bool = False

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
                    "ratio", "pair", "game", "seed", "target_color",
                    "outcome", "actions", "reason", "pgn_path",
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
                    "elapsed_seconds", "ips", "time_capped",
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
        seed: int,
        target_white: bool,
        result: GameResult,
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
                    "ratio", "pair", "game", "seed", "target_color",
                    "outcome", "actions", "reason", "pgn_path",
                ),
            )
            writer.writerow(
                {
                    "ratio": ratio,
                    "pair": pair_number,
                    "game": game_number,
                    "seed": seed,
                    "target_color": "white" if target_white else "black",
                    "outcome": result.outcome,
                    "actions": result.actions,
                    "reason": result.reason,
                    "pgn_path": pgn_path,
                }
            )
            output.flush()


def ratio_slug(value: float) -> str:
    return format(value, ".6g").replace(".", "p")


def command_for_seed(template: str, seed: int) -> str:
    return template.replace("{seed}", str(seed))


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
    zero_time_ms: int,
    ips_ratio: float,
    cap_factor: float,
    absolute_cap_ms: int,
) -> int:
    if ips_ratio <= 0:
        return absolute_cap_ms
    predicted_cap = math.ceil(cap_factor * zero_time_ms / ips_ratio)
    return max(zero_time_ms, min(absolute_cap_ms, predicted_cap))


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


def print_live(result: RatioResult, throughput: Throughput) -> None:
    print(
        f"[m={result.ratio:g}] games {result.completed_games}: "
        f"target {result.game_wins}-{result.game_draws}-{result.game_losses} "
        f"(void {result.void_games}); pairs "
        f"{result.pair_wins}-{result.pair_ties}-{result.pair_losses} "
        f"(void {result.void_pairs}, mean score "
        f"{format_ratio(result.mean_pair_score)}); target/zero IPS "
        f"{format_ratio(throughput.aggregate_ratio)}",
        flush=True,
    )


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


async def calibrate(experiment: Experiment) -> None:
    args = experiment.args
    seed = args.seed_start
    zero = autoplay.EngineProcess(
        "zero calibration", command_for_seed(args.zero, seed), args.timeout
    )
    target = autoplay.EngineProcess(
        "target calibration", command_for_seed(args.target, seed), args.timeout
    )
    commands: asyncio.Queue[str] = asyncio.Queue()
    game = experiment.rules.game.from_pgn(experiment.openings[0])
    position = autoplay.uci_position(game)
    try:
        await start_engines((zero, target))
        await zero.choose(position, [], args.zero_movetime, commands)
        zero_sample = search_sample(zero)
        experiment.throughput.add_zero(zero_sample)
        await target.choose(position, [], args.zero_movetime, commands)
        target_sample = search_sample(target)
        experiment.throughput.add_pair(target_sample, zero_sample)
    finally:
        await asyncio.gather(zero.close(), target.close())
    print(
        "Calibration: "
        f"zero={zero_sample.ips:.0f} IPS, target={target_sample.ips:.0f} IPS, "
        f"target/zero={target_sample.ips / zero_sample.ips:.3f}",
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
    seed: int,
    opening: str,
    target_white: bool,
) -> GameResult:
    args = experiment.args
    rules = experiment.rules
    target = autoplay.EngineProcess(
        "target", command_for_seed(args.target, seed), args.timeout
    )
    zero = autoplay.EngineProcess("zero", command_for_seed(args.zero, seed), args.timeout)
    meter = autoplay.EngineProcess(
        "zero meter", command_for_seed(args.zero, seed), args.timeout
    )
    players = [target, zero] if target_white else [zero, target]
    commands: asyncio.Queue[str] = asyncio.Queue()
    game = rules.game.from_pgn(opening)
    metadata = dict(game.metadata)
    metadata.update(
        {
            "event": "Iteration-ratio policy test",
            "site": "Local",
            "date": datetime.now().strftime("%Y.%m.%d"),
            "round": f"{ratio:g}.{pair_number}.{1 if target_white else 2}",
            "white": "Target" if target_white else "Zero",
            "black": "Zero" if target_white else "Target",
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
        await start_engines((target, zero, meter))
        for action_number in range(1, args.max_actions + 1):
            actions = action_number
            _, black_to_move = game.get_current_present()
            color_index = int(black_to_move)
            player = players[color_index]
            target_turn = player is target
            if target_turn:
                try:
                    meter_moves = await meter.choose(
                        initial_position, history, args.zero_movetime, commands
                    )
                except (autoplay.ProtocolError, ValueError) as exc:
                    outcome = "void"
                    reason = f"zero meter failure: {exc}"
                    break
                if not meter_moves:
                    status = await autoplay.adjudicate(
                        autoplay.snapshot_pgn(game), args.module_dir
                    )
                    if status in ("white", "black", "draw"):
                        outcome = winner_to_target_outcome(status, target_white)
                        reason = f"zero meter detected terminal position: {status}"
                    else:
                        outcome = "void"
                        reason = (
                            "zero meter returned no move in a nonterminal "
                            f"position ({status})"
                        )
                    break
                reference = search_sample(meter)
                experiment.throughput.add_zero(reference)
                requested, iteration_limit = iteration_budget(
                    reference.iterations, ratio
                )
                current_ips_ratio = experiment.throughput.cap_ratio or 1.0
                cap_ms = target_time_cap_ms(
                    args.zero_movetime,
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
                    experiment.throughput.add_pair(sample, reference)
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
                        }
                    )
            else:
                try:
                    moves = await zero.choose(
                        initial_position, history, args.zero_movetime, commands
                    )
                except (autoplay.ProtocolError, ValueError) as exc:
                    outcome = "win"
                    reason = f"zero failure: {exc}"
                    break
                if moves:
                    sample = search_sample(zero)
                    experiment.throughput.add_zero(sample)
                    experiment.record_search(
                        {
                            "ratio": ratio,
                            "pair": pair_number,
                            "game": game_number,
                            "action": action_number,
                            "role": "zero",
                            "reference_iterations": "",
                            "reference_elapsed_seconds": "",
                            "reference_ips": "",
                            "requested_iterations": "",
                            "iteration_limit": "",
                            "cap_ips_ratio": "",
                            "movetime_cap_ms": args.zero_movetime,
                            "actual_iterations": sample.iterations,
                            "elapsed_seconds": sample.seconds,
                            "ips": sample.ips,
                            "time_capped": "",
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
                    reason = f"zero returned no move in nonterminal position ({status})"
                break
            error = autoplay.apply_action(rules, game, moves)
            if error:
                if target_turn:
                    outcome = "loss"
                    reason = f"target illegal action: {error}"
                else:
                    outcome = "win"
                    reason = f"zero illegal action: {error}"
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
        await asyncio.gather(target.close(), zero.close(), meter.close())


async def play_pair(
    experiment: Experiment,
    result: RatioResult,
    semaphore: asyncio.Semaphore,
    pair_number: int,
) -> list[GameResult]:
    args = experiment.args
    seed = args.seed_start + pair_number - 1
    opening = experiment.openings[(pair_number - 1) % len(experiment.openings)]

    async def one(target_white: bool) -> GameResult:
        game_number = (pair_number - 1) * 2 + (1 if target_white else 2)
        async with semaphore:
            game = await play_game(
                experiment,
                result.ratio,
                pair_number,
                game_number,
                seed,
                opening,
                target_white,
            )
        result.add_game(game.outcome)
        experiment.record_game(
            result.ratio, pair_number, game_number, seed, target_white, game
        )
        print_live(result, experiment.throughput)
        return game

    games = await asyncio.gather(one(True), one(False))
    result.add_pair(games)
    print_live(result, experiment.throughput)
    return games


async def run_ratio(experiment: Experiment, ratio: float) -> RatioResult:
    args = experiment.args
    result = RatioResult(ratio)
    experiment.results.append(result)
    semaphore = asyncio.Semaphore(args.workers)
    looks = args.look_pairs
    # Spend more alpha at the larger, more informative looks while keeping
    # the family-wise total bounded by args.alpha.
    alpha_at_looks = [args.alpha * look / sum(looks) for look in looks]
    next_look = 0
    pair_number = 1
    parallel_pairs = max(1, math.ceil(args.workers / 2))
    print(
        f"\nStarting m={ratio:g}; planned pair looks={looks}, "
        f"alpha spending={[round(value, 6) for value in alpha_at_looks]}",
        flush=True,
    )
    while pair_number <= args.max_pairs:
        batch_size = min(parallel_pairs, args.max_pairs - pair_number + 1)
        tasks = [
            asyncio.create_task(
                play_pair(experiment, result, semaphore, pair_number + offset)
            )
            for offset in range(batch_size)
        ]
        await asyncio.gather(*tasks)
        pair_number += batch_size

        while next_look < len(looks) and result.valid_pairs >= looks[next_look]:
            alpha_at_look = alpha_at_looks[next_look]
            decision = evaluate_ratio(
                result, alpha_at_look, args.noninferiority_margin
            )
            look = looks[next_look]
            print(
                f"Statistical look at {result.valid_pairs} valid pairs "
                f"(planned {look}): superiority p="
                f"{format_probability(result.superiority_p)}, non-inferiority p="
                f"{format_probability(result.noninferiority_p)}, inferiority p="
                f"{format_probability(result.inferiority_p)}; "
                f"threshold={alpha_at_look:.4g}",
                flush=True,
            )
            next_look += 1
            if decision:
                result.decision = decision
                return result

        if result.completed_games >= 2 * args.max_pairs:
            break

    result.decision = "inconclusive"
    if result.valid_pairs:
        evaluate_ratio(result, alpha_at_looks[-1], args.noninferiority_margin)
    return result


def print_final_summary(experiment: Experiment) -> None:
    print("\n=== Iteration-ratio summary ===")
    for result in experiment.results:
        if result.valid_pairs and result.superiority_p is None:
            evaluate_ratio(
                result,
                experiment.args.alpha,
                experiment.args.noninferiority_margin,
            )
        print(
            f"m={result.ratio:g}: {result.decision}; games "
            f"{result.game_wins}-{result.game_draws}-{result.game_losses} "
            f"(void {result.void_games}); pairs "
            f"{result.pair_wins}-{result.pair_ties}-{result.pair_losses}, "
            f"mean score={format_ratio(result.mean_pair_score)}; "
            f"p(superior)={format_probability(result.superiority_p)}, "
            f"p(noninferior)={format_probability(result.noninferiority_p)}, "
            f"p(inferior)={format_probability(result.inferiority_p)}"
        )
    established = [
        result.ratio
        for result in experiment.results
        if result.decision in ("better", "noninferior")
    ]
    if established:
        print(f"Most aggressive established ratio: m={min(established):g}")
    else:
        print("Most aggressive established ratio: none")
    print(
        f"Actual aggregate target IPS: {experiment.throughput.target_ips:.1f}\n"
        f"Actual aggregate zero IPS:   {experiment.throughput.zero_ips:.1f}\n"
        f"Actual target/zero IPS ratio: "
        f"{format_ratio(experiment.throughput.aggregate_ratio)}\n"
        f"Paired geometric IPS ratio:   "
        f"{format_ratio(experiment.throughput.paired_geometric_ratio)}"
    )
    if experiment.interrupted:
        print("Run interrupted; unfinished games were excluded.")
    print(f"Logs: {experiment.output_dir.resolve()}")


async def run_experiment(experiment: Experiment) -> int:
    experiment.initialize_logs()
    try:
        await calibrate(experiment)
        for ratio in experiment.args.ratios:
            result = await run_ratio(experiment, ratio)
            print(f"m={ratio:g} decision: {result.decision}", flush=True)
            if result.decision not in ("better", "noninferior"):
                break
        return 0
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
    if not values or any(value <= 0 for value in values):
        raise argparse.ArgumentTypeError("all ratios must be positive")
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--target",
        required=True,
        help="target engine command; optional {seed} is replaced for each pair",
    )
    parser.add_argument(
        "--zero",
        default="./build/5dchess zero --seed {seed}",
        help="zero command; optional {seed} is replaced for each pair",
    )
    parser.add_argument(
        "--ratios", type=parse_float_list, default=parse_float_list("1,.75,.5,.25")
    )
    parser.add_argument("--zero-movetime", type=int, default=400)
    parser.add_argument(
        "--cap-factor",
        type=float,
        default=2.0,
        help="target wall cap = factor * zero time / measured target/zero IPS",
    )
    parser.add_argument("--max-target-movetime", type=int, default=10_000)
    parser.add_argument("--timeout", type=int, default=2_000)
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--max-actions", type=int, default=500)
    parser.add_argument("--seed-start", type=int, default=1)
    parser.add_argument("--module-dir", type=Path, default=Path("build"))
    parser.add_argument(
        "--opening",
        action="append",
        type=Path,
        default=[],
        help="starting 5DPGN; repeat for an opening cycle",
    )
    parser.add_argument(
        "--look-pairs",
        type=parse_int_list,
        default=parse_int_list("8,16,32,64,128,256"),
        help="predeclared valid-pair checkpoints",
    )
    parser.add_argument("--max-pairs", type=int, default=256)
    parser.add_argument("--alpha", type=float, default=0.05)
    parser.add_argument(
        "--noninferiority-margin",
        type=float,
        default=0.10,
        help="largest tolerated deficit in mean normalized pair score",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="default: logs/iteration-ratio-YYYYmmdd-HHMMSS",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.zero_movetime <= 0 or args.max_target_movetime <= 0 or args.timeout <= 0:
        parser.error("time limits must be positive")
    if args.cap_factor <= 0 or args.workers <= 0 or args.max_actions <= 0:
        parser.error("cap factor, workers, and max actions must be positive")
    if not 0 < args.alpha < 1:
        parser.error("--alpha must be between zero and one")
    if not 0 < args.noninferiority_margin < 0.5:
        parser.error("--noninferiority-margin must be between zero and 0.5")
    if args.max_pairs < args.look_pairs[-1]:
        parser.error("--max-pairs must be at least the final --look-pairs value")
    if args.output_dir is None:
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        args.output_dir = Path("logs") / f"iteration-ratio-{stamp}"
    openings = []
    if args.opening:
        for path in args.opening:
            try:
                openings.append(path.read_text())
            except OSError as exc:
                parser.error(f"cannot read opening {path}: {exc}")
    else:
        openings.append('[Board "Standard - Turn Zero"]')
    rules = autoplay.load_rules(args.module_dir)
    experiment = Experiment(args, rules, openings, args.output_dir)
    try:
        return asyncio.run(run_experiment(experiment))
    except KeyboardInterrupt:
        # asyncio cancellation runs run_experiment's finally block first.
        return 130


if __name__ == "__main__":
    raise SystemExit(main())

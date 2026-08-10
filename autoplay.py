#!/usr/bin/env python3
"""Play one headless 5DUCI game between two engine subprocesses."""

from __future__ import annotations

import argparse
import asyncio
import csv
import contextlib
from collections import deque
import importlib
import multiprocessing
import re
import shlex
import sys
import time
from pathlib import Path


MOVE_RE = re.compile(
    r"^\((-?\d+)T(-?\d+)\)([a-h])([1-8])"
    r"(?:\((-?\d+)T(-?\d+)\))?([a-h])([1-8])([A-Za-z])?$"
)


class ProtocolError(RuntimeError):
    """An engine crashed, timed out, or violated the expected 5DUCI exchange."""

    pass


class EngineProcess:
    """One 5DUCI engine subprocess and its request/response lifecycle."""

    def __init__(self, name: str, command: str, timeout: float):
        self.name = name
        self.command = command
        self.argv = shlex.split(command)
        self.timeout = timeout
        self.process: asyncio.subprocess.Process | None = None
        self.phase = "created"
        self.protocol_log = deque(maxlen=24)
        self.stderr_log = deque(maxlen=24)
        self.stderr_task: asyncio.Task | None = None
        self.last_mcts_stats: dict[str, str] = {}
        self.last_go_seconds: float | None = None

    def _record(self, direction: str, line: str) -> None:
        self.protocol_log.append(f"{direction} {line}")

    def diagnostic(self) -> str:
        """Return compact process context suitable for a failure message."""

        protocol_tail = list(self.protocol_log)[-8:]
        stderr_tail = list(self.stderr_log)[-8:]
        return (
            f"phase={self.phase}, returncode={self.process.returncode if self.process else None}; "
            f"protocol={' | '.join(protocol_tail) or '<none>'}; "
            f"stderr={' | '.join(stderr_tail) or '<none>'}"
        )

    async def _drain_stderr(self) -> None:
        assert self.process and self.process.stderr
        while True:
            raw = await self.process.stderr.readline()
            if not raw:
                return
            line = raw.decode(errors="replace").rstrip()
            if line:
                self.stderr_log.append(line)

    async def start(self) -> None:
        if not self.argv:
            raise ProtocolError(f"{self.name}: empty command")
        try:
            self.process = await asyncio.create_subprocess_exec(
                *self.argv,
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                start_new_session=sys.platform != "win32",
            )
        except OSError as exc:
            raise ProtocolError(f"{self.name}: cannot start {self.command!r}: {exc}") from exc
        self.stderr_task = asyncio.create_task(self._drain_stderr())
        self.phase = "initializing"
        await self.send("5duci")
        await self.wait_for("5duciok", self.timeout)
        self.phase = "readying"
        await self.send("isready")
        await self.wait_for("readyok", self.timeout)
        self.phase = "idle"

    async def send(self, line: str) -> None:
        if not self.process or not self.process.stdin:
            raise ProtocolError(f"{self.name}: process is not started; {self.diagnostic()}")
        if self.process.returncode is not None:
            raise ProtocolError(f"{self.name}: exited with {self.process.returncode}; {self.diagnostic()}")
        self._record(">", line)
        self.process.stdin.write((line + "\n").encode())
        await self.process.stdin.drain()

    async def wait_for(self, expected: str | tuple[str, ...], timeout: float) -> str:
        assert self.process and self.process.stdout
        choices = (expected,) if isinstance(expected, str) else expected

        async def read_response() -> str:
            # Engines may emit any number of informational lines before the
            # response for which the UI is waiting.
            while True:
                raw = await self.process.stdout.readline()
                if not raw:
                    code = await self.process.wait()
                    raise ProtocolError(f"{self.name}: exited with {code}; {self.diagnostic()}")
                line = raw.decode(errors="replace").strip()
                self._record("<", line)
                if line.startswith("info mcts_stats "):
                    self.last_mcts_stats = dict(
                        token.split("=", 1)
                        for token in line.split()[2:]
                        if "=" in token
                    )
                if any(line == item or line.startswith(item + " ") for item in choices):
                    return line
                if not line.startswith("info "):
                    print(f"[{self.name}] ignored: {line}", file=sys.stderr)

        try:
            return await asyncio.wait_for(read_response(), timeout)
        except TimeoutError as exc:
            raise ProtocolError(
                f"{self.name}: timed out in {self.phase} waiting for {'/'.join(choices)}; "
                f"{self.diagnostic()}"
            ) from exc

    async def new_game(self) -> None:
        self.phase = "new_game"
        await self.send("5ducinewgame")
        await self.send("isready")
        await self.wait_for("readyok", self.timeout)
        self.phase = "idle"

    async def choose(
        self, position: str, history: list[str], movetime_ms: int, commands: asyncio.Queue[str]
    ) -> list[str] | None:
        self.phase = "searching"
        request = f"position {position}"
        if history:
            request += " moves " + " ".join(history)
        await self.send(request)
        self.last_mcts_stats = {}
        self.last_go_seconds = None
        go_started = time.perf_counter()
        await self.send(f"go movetime {movetime_ms}")

        # Wait for engine output and terminal input concurrently.  Keeping
        # this orchestration in Python makes Ctrl+C responsive even while the
        # engine is busy in native search code.
        response = asyncio.create_task(
            self.wait_for(("bestmove", "nobestmove"), movetime_ms / 1000 + self.timeout)
        )
        command = None
        stop_deadline = None
        try:
            while not response.done():
                command = asyncio.create_task(commands.get())
                pending = [response, command]
                if stop_deadline:
                    pending.append(stop_deadline)
                done, _ = await asyncio.wait(pending, return_when=asyncio.FIRST_COMPLETED)
                if response in done:
                    command.cancel()
                    break
                if stop_deadline and stop_deadline in done:
                    command.cancel()
                    raise ProtocolError(
                        f"{self.name}: did not respond to stop within {self.timeout:g}s; "
                        f"{self.diagnostic()}"
                    )
                if command.result().strip().lower() == "stop":
                    print(f"Stopping {self.name}'s search...", file=sys.stderr)
                    await self.send("stop")
                    if stop_deadline is None:
                        # An engine that ignores the protocol's stop command
                        # must not be allowed to hang the match indefinitely.
                        stop_deadline = asyncio.create_task(asyncio.sleep(self.timeout))
                else:
                    print("Type 'stop' to force the thinking engine to move.", file=sys.stderr)
            line = await response
        finally:
            self.last_go_seconds = time.perf_counter() - go_started
            if command and not command.done():
                command.cancel()
            if stop_deadline and not stop_deadline.done():
                stop_deadline.cancel()
            if not response.done():
                response.cancel()
            with contextlib.suppress(asyncio.CancelledError, ProtocolError):
                await response
        if line == "nobestmove":
            self.phase = "idle"
            return None
        moves = line.split()[1:]
        self.phase = "idle"
        return moves or None

    async def close(self) -> None:
        if not self.process:
            return
        if self.process.returncode is None:
            try:
                await self.send("quit")
                await asyncio.wait_for(self.process.wait(), 1.0)
            except (ProtocolError, TimeoutError, BrokenPipeError):
                # quit is cooperative; kill is the final cleanup fallback.
                self.process.kill()
                await self.process.wait()
        if self.stderr_task:
            self.stderr_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self.stderr_task
            self.stderr_task = None
        self.phase = "closed"


METRICS_FIELDS = (
    "game",
    "action",
    "move_number",
    "color",
    "engine_name",
    "engine_command",
    "requested_movetime_ms",
    "wall_time_seconds",
    "nodes_visited",
    "nodes_per_wall_second",
    "engine_search_seconds",
    "engine_nodes_per_second",
    "status",
)


def initialize_metrics(path: Path) -> None:
    """Create a fresh per-go metrics file for this autoplay invocation."""

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as output:
        csv.DictWriter(output, fieldnames=METRICS_FIELDS).writeheader()


def record_go_metrics(
    path: Path,
    player: EngineProcess,
    game_number: int,
    action_number: int,
    move_number: int,
    color: str,
    movetime_ms: int,
    status: str,
) -> None:
    """Append one completed or failed `go` command, flushing it immediately."""

    nodes_text = player.last_mcts_stats.get("nodes_visited", "")
    wall_seconds = player.last_go_seconds
    nodes_per_wall_second = ""
    if nodes_text and wall_seconds and wall_seconds > 0.0:
        nodes_per_wall_second = float(nodes_text) / wall_seconds
    row = {
        "game": game_number,
        "action": action_number,
        "move_number": move_number,
        "color": color,
        "engine_name": player.name,
        "engine_command": player.command,
        "requested_movetime_ms": movetime_ms,
        "wall_time_seconds": wall_seconds if wall_seconds is not None else "",
        "nodes_visited": nodes_text,
        "nodes_per_wall_second": nodes_per_wall_second,
        "engine_search_seconds": player.last_mcts_stats.get("elapsed_seconds", ""),
        "engine_nodes_per_second": player.last_mcts_stats.get("nodes_per_second", ""),
        "status": status,
    }
    with path.open("a", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=METRICS_FIELDS)
        writer.writerow(row)
        output.flush()


def load_rules(module_dir: Path):
    """Import the pybind11 game-rules module built by CMake."""

    sys.path.insert(0, str(module_dir.resolve()))
    try:
        return importlib.import_module("engine")
    except ImportError as exc:
        raise SystemExit(
            f"Cannot import the Python engine module from {module_dir}.\n"
            "Build it for this Python interpreter with:\n"
            f"  cmake -S . -B {module_dir} -DPYMODULE=ON -DTEST=ON "
            f"-DCMAKE_BUILD_TYPE=Release -DPYTHON_EXECUTABLE={sys.executable}\n"
            f"  cmake --build {module_dir} -j"
        ) from exc


def parse_move(rules, text: str):
    """Convert one 5DUCI LAN token into the binding's ext_move object."""

    match = MOVE_RE.fullmatch(text)
    if not match:
        raise ValueError(f"malformed move {text!r}")
    line0, turn0, file0, rank0, line1, turn1, file1, rank1, promotion = match.groups()
    line1, turn1 = (line0, turn0) if line1 is None else (line1, turn1)
    source = rules.vec4(ord(file0) - 97, int(rank0) - 1, int(turn0), int(line0))
    target = rules.vec4(ord(file1) - 97, int(rank1) - 1, int(turn1), int(line1))
    piece_names = {
        "Q": "QUEEN_W", "R": "ROOK_W", "B": "BISHOP_W", "N": "KNIGHT_W",
        "U": "UNICORN_W", "D": "DRAGON_W", "W": "BRAWN_W",
        "S": "PRINCESS_W", "Y": "ROYAL_QUEEN_W", "C": "COMMON_KING_W",
    }
    symbol = (promotion or "Q").upper()
    if symbol not in piece_names:
        raise ValueError(f"unknown promotion piece {symbol!r}")
    piece = getattr(rules.Piece, piece_names[symbol])
    return rules.ext_move(source, target, piece)


def apply_action(rules, game, moves: list[str]) -> str | None:
    """Apply and submit a complete bestmove action, rolling back on error."""

    applied = 0
    try:
        for text in moves:
            if text == "submit" or not game.apply_move(parse_move(rules, text)):
                raise ValueError(f"illegal move {text!r}")
            applied += 1
        if not game.submit():
            raise ValueError("action cannot be submitted")
        return None
    except (ValueError, RuntimeError) as exc:
        for _ in range(applied):
            game.undo()
        return str(exc)


def uci_position(game) -> str:
    """Serialize the game's current multiverse as a fresh 5DUCI FEN base.

    A loaded PGN may already contain many actions.  Replaying and translating
    that notation is unnecessary: get_current_fen() exposes the resulting
    boards, including unmoved-piece markers.  We send them together with board
    size and odd/even timeline numbering as the new initial position.  The
    `history` list then contains only actions played by this autoplay session.
    """

    size_x, size_y = game.get_board_size()
    parity = game.metadata.get("timeline", "odd")

    # state::show_fen() preserves unmoved markers (`*`).  The older
    # get_current_boards() binding omitted them and therefore changed pawn,
    # castling, and related move rights while reconstructing the position.
    blocks = " ".join(game.get_current_fen().splitlines())
    return f"size {size_x}x{size_y} {parity} fen {blocks}"


def snapshot_pgn(game) -> str:
    """Create a history-free PGN snapshot suitable for isolated adjudication."""

    size_x, size_y = game.get_board_size()
    parity = game.metadata.get("timeline", "odd")
    boards = game.get_current_fen().rstrip()
    return f'[Size "{size_x}x{size_y}"]\n[Timeline "{parity}"]\n{boards}\n'


def status_worker(module_dir: str, pgn: str, sender) -> None:
    """Compute match status outside the controller so it can be terminated."""

    sys.path.insert(0, module_dir)
    rules = importlib.import_module("engine")
    status = rules.game.from_pgn(pgn).get_match_status()
    if status == rules.match_status_t.WHITE_WINS:
        sender.send("white")
    elif status == rules.match_status_t.BLACK_WINS:
        sender.send("black")
    elif status == rules.match_status_t.STALEMATE:
        sender.send("draw")
    else:
        sender.send("playing")
    sender.close()


def save_adjudication_position(game) -> Path:
    """Persist the exact position passed to get_match_status() for diagnosis."""

    logs = Path("logs")
    logs.mkdir(parents=True, exist_ok=True)
    output = logs / "adjudication.5dpgn"
    output.write_text(snapshot_pgn(game))
    return output.resolve()


def save_protocol_failure(
    game, players: list[EngineProcess], result: str, show_flags: int, game_number: int
) -> Path | None:
    """Persist protocol diagnostics and the partial PGN for later inspection."""

    logs = Path("logs")
    try:
        logs.mkdir(parents=True, exist_ok=True)
        output = logs / f"protocol-failure-{game_number:04d}.txt"
        lines = [f"Result: {result}", "", "Engines:"]
        for color, player in zip(("white", "black"), players):
            lines.append(f"[{color}] command={player.command!r} name={player.name!r}")
            lines.append(f"{player.diagnostic()}")
        lines.extend(("", "Partial game PGN:", game.show_pgn(show_flags)))
        output.write_text("\n".join(lines) + "\n")
        return output.resolve()
    except OSError as exc:
        print(f"Could not save protocol diagnostics: {exc}", file=sys.stderr)
        return None


async def adjudicate(pgn: str, module_dir: Path) -> str:
    """Return match status without blocking the controller's event loop."""

    context = multiprocessing.get_context("spawn")
    receiver, sender = context.Pipe(duplex=False)
    process = context.Process(
        target=status_worker,
        args=(str(module_dir.resolve()), pgn, sender),
    )
    process.start()
    sender.close()
    try:
        while True:
            if receiver.poll():
                try:
                    return receiver.recv()
                except EOFError:
                    process.join(0.2)
                    return f"error:{process.exitcode}"
            if not process.is_alive():
                process.join()
                return f"error:{process.exitcode}"
            await asyncio.sleep(0.05)
    finally:
        receiver.close()
        if process.is_alive():
            process.terminate()
        process.join(1.0)
        if process.is_alive():
            process.kill()
            process.join()


async def play(args, rules, game_number: int = 1) -> str:
    players = [
        EngineProcess(args.white_name, args.white, args.timeout),
        EngineProcess(args.black_name, args.black, args.timeout),
    ]
    if args.game_file:
        try:
            initial_pgn = args.game_file.read_text()
        except OSError as exc:
            raise SystemExit(f"Cannot read game file {args.game_file}: {exc}") from exc
    else:
        initial_pgn = args.game_text or '[Board "Standard - Turn Zero"]'
    try:
        game = rules.game.from_pgn(initial_pgn)
    except RuntimeError as exc:
        raise SystemExit(f"Cannot parse initial game: {exc}") from exc
    initial_position = uci_position(game)
    # The 5D "present" may move backwards after branching, so it cannot be
    # used as a monotonically increasing display counter.  Capture only the
    # starting side/number, then advance labels by autoplay action count.
    first_move_number, first_is_black = game.get_current_present()
    # 5DUCI requires complete history relative to the position base.  Each
    # bestmove omits the mandatory submit token, so the controller adds it.
    history: list[str] = []
    show_flags = rules.SHOW_CAPTURE | rules.SHOW_PROMOTION | rules.SHOW_SHORT
    commands: asyncio.Queue[str] = asyncio.Queue()
    loop = asyncio.get_running_loop()
    watching_stdin = False

    def read_command() -> None:
        # add_reader avoids a blocking input() call and lets engine output,
        # user commands, and signals share the same event loop.
        line = sys.stdin.readline()
        if line:
            commands.put_nowait(line)
        else:
            loop.remove_reader(sys.stdin)

    try:
        loop.add_reader(sys.stdin, read_command)
        watching_stdin = True
    except (AttributeError, NotImplementedError, OSError):
        pass
    result = "draw: action limit reached"

    def player_label(index: int) -> str:
        player = players[index]
        return player.name or player.command

    def winner(index: int) -> str:
        player = players[index]
        identity = f"{player.name}, {player.command}" if player.name else player.command
        return f"{'black' if index else 'white'}({identity}) wins"

    def identity(index: int) -> str:
        player = players[index]
        return f"{player.name}, {player.command}" if player.name else player.command

    if game_number == 1:
        print(f"Match: white({identity(0)}) vs black({identity(1)})\n")
        print("Initial game PGN:\n")
        print(game.show_pgn(show_flags))
    if args.games is not None:
        print(f"Game {game_number}/{args.games}\n")
    else:
        print("Starting match...\n")

    try:
        outcome = "cap"
        startup_results = await asyncio.gather(
            *(player.start() for player in players), return_exceptions=True
        )
        startup_errors = [item for item in startup_results if isinstance(item, Exception)]
        if startup_errors:
            outcome = "protocol"
            result = f"protocol failure during startup: {startup_errors[0]}"
            failure_file = save_protocol_failure(game, players, result, show_flags, game_number)
            print(f"\nResult: {result}\n", file=sys.stderr)
            if failure_file:
                print(f"Protocol diagnostics saved at\n  {failure_file}", file=sys.stderr)
            print("Partial game PGN:\n")
            print(game.show_pgn(show_flags))
            return outcome

        new_game_results = await asyncio.gather(
            *(player.new_game() for player in players), return_exceptions=True
        )
        new_game_errors = [item for item in new_game_results if isinstance(item, Exception)]
        if new_game_errors:
            outcome = "protocol"
            result = f"protocol failure during new game: {new_game_errors[0]}"
            failure_file = save_protocol_failure(game, players, result, show_flags, game_number)
            print(f"\nResult: {result}\n", file=sys.stderr)
            if failure_file:
                print(f"Protocol diagnostics saved at\n  {failure_file}", file=sys.stderr)
            print("Partial game PGN:\n")
            print(game.show_pgn(show_flags))
            return outcome

        for turn in range(1, args.max_actions + 1):
            move_number, black_to_move = game.get_current_present()
            index = int(black_to_move)
            player = players[index]
            try:
                moves = await player.choose(initial_position, history, args.movetime, commands)
            except ProtocolError as exc:
                record_go_metrics(
                    args.metrics_csv, player, game_number, turn, move_number,
                    "black" if index else "white", args.movetime, "protocol_error")
                outcome = "protocol"
                result = f"protocol failure from {player_label(index)}: {exc}"
                failure_file = save_protocol_failure(game, players, result, show_flags, game_number)
                if failure_file:
                    print(f"Protocol diagnostics saved at\n  {failure_file}", file=sys.stderr)
                break
            record_go_metrics(
                args.metrics_csv, player, game_number, turn, move_number,
                "black" if index else "white", args.movetime,
                "bestmove" if moves else "nobestmove")
            if not moves:
                adjudication_file = save_adjudication_position(game)
                print(
                    f"{player_label(index)} returned nobestmove; adjudicating position saved at\n"
                    f"  {adjudication_file}",
                    flush=True,
                )
                status = await adjudicate(adjudication_file.read_text(), args.module_dir)
                if status == "white":
                    outcome = "white"
                    result = winner(0)
                elif status == "black":
                    outcome = "black"
                    result = winner(1)
                elif status == "draw":
                    outcome = "draw"
                    result = "draw: stalemate"
                elif status.startswith("error:"):
                    outcome = "error"
                    exit_code = status.partition(":")[2]
                    result = (
                        f"no result: adjudicator exited with {exit_code}; "
                        f"position retained at {adjudication_file}"
                    )
                else:
                    winning_index = 1 - index
                    outcome = "black" if winning_index else "white"
                    result = (
                        f"{winner(winning_index)}: {player_label(index)} returned no move; "
                        "position is not terminal"
                    )
                break
            error = apply_action(rules, game, moves)
            if error:
                winning_index = 1 - index
                outcome = "black" if winning_index else "white"
                result = f"{winner(winning_index)}: {player_label(index)} {error}"
                break
            history.extend(moves)
            history.append("submit")
            ply = int(first_is_black) + turn - 1
            move_number = first_move_number + ply // 2
            color = "b" if ply % 2 else "w"
            names = [item.name for item in players]
            if player.name:
                width = max(map(len, names))
                print(f"{move_number}{color}. {player.name:<{width}}: {' '.join(moves)}")
            else:
                print(f"{move_number}{color}. {' '.join(moves)}")
        else:
            result = "draw: action limit reached"

        print(f"\nResult: {result}\n")
        print(game.show_pgn(show_flags))
        return outcome
    except asyncio.CancelledError:
        print("\nInterrupted: quitting both engines.\n", file=sys.stderr)
        print("Partial game PGN:\n")
        print(game.show_pgn(show_flags))
        raise
    finally:
        if watching_stdin:
            loop.remove_reader(sys.stdin)
        await asyncio.gather(*(player.close() for player in players))


def print_summary(counts: dict[str, int], requested: int) -> None:
    """Print aggregate counts and rates for all fully completed games."""

    completed = sum(counts.values())
    print(f"\nSeries summary: {completed}/{requested} games completed")
    for key, label in (
        ("white", "White wins"),
        ("black", "Black wins"),
        ("draw", "Draws"),
        ("cap", "Action caps reached"),
        ("error", "Adjudication errors"),
        ("protocol", "Protocol failures"),
    ):
        rate = 100 * counts[key] / completed if completed else 0.0
        print(f"{label}: {counts[key]} ({rate:.1f}%)")


async def run(args, rules) -> int:
    """Run one game, or a requested series of independent games."""

    initialize_metrics(args.metrics_csv)
    print(f"Per-go metrics: {args.metrics_csv.resolve()}")
    if args.games is None:
        await play(args, rules)
        return 0

    counts = {"white": 0, "black": 0, "draw": 0, "cap": 0, "error": 0, "protocol": 0}
    try:
        for game_number in range(1, args.games + 1):
            outcome = await play(args, rules, game_number)
            counts[outcome] += 1
    finally:
        # asyncio cancellation from Ctrl+C passes through this block after the
        # current game has printed its partial PGN and closed both engines.
        print_summary(counts, args.games)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--white", default="./build/cli uci mcts", help="white engine command")
    parser.add_argument("--black", default="./build/cli uci monkey", help="black engine command")
    parser.add_argument("--white-name", default="")
    parser.add_argument("--black-name", default="")
    game_group = parser.add_mutually_exclusive_group()
    game_group.add_argument("-g", "--game", dest="game_file", type=Path, help="initial .5dpgn file")
    game_group.add_argument("-m", dest="game_text", help="initial 5DPGN text")
    parser.add_argument("--module-dir", type=Path, default=Path("build"))
    parser.add_argument("--movetime", type=int, default=1000, help="milliseconds per action")
    parser.add_argument(
        "--timeout",
        type=float,
        default=2.0,
        help="protocol grace period in seconds",
    )
    parser.add_argument("--max-actions", type=int, default=500)
    parser.add_argument("-n", "--games", type=int, help="number of games to play")
    parser.add_argument(
        "--metrics-csv",
        type=Path,
        default=Path("logs/autoplay-go-metrics.csv"),
        help="CSV output for per-go timing and MCTS visit counts",
    )
    args = parser.parse_args()
    if args.movetime <= 0 or args.timeout <= 0 or args.max_actions <= 0:
        parser.error("time and action limits must be positive")
    if args.games is not None and args.games <= 0:
        parser.error("--games must be positive")
    try:
        return asyncio.run(run(args, load_rules(args.module_dir)))
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())

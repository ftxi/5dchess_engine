"""Visible contracts for five scripts and the real compiled Python binding.

See README.md beside this file for what passing each group means and excludes.
Run: python -m unittest discover -s scripts/test -v
test.py is an ad hoc demonstration and is deliberately excluded.

CLI checks launch real scripts and read public output, never database tables
or script helpers. This file also acts as a scripted 5DUCI engine subprocess:
only that external process is substituted, not the controller or game rules.
"""

import csv
import importlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
BUILD = ROOT / "build"
THIS_FILE = Path(__file__).resolve()
STANDARD_GAME = '[Board "Standard - Turn Zero"]'
SMALL_GAME = '[Size "4x4"]\n[Board "Custom"]\n[Mode "5D"]\n[nbrk/3p*/P*3/KRBN:0:1:w]'


def scripted_engine(arguments):
    """A protocol peer with legal first plies, fixed metrics, and fault modes.

    Records received commands so tests can check externally sent budgets,
    history, seed expansion, and cooperative shutdown. Two plies suffice for
    our capped games; this is not a chess implementation.
    """
    mode, trace, iterations, seed = arguments
    sys.path.insert(0, str(SCRIPTS))
    try:
        import cpu_affinity
    except ModuleNotFoundError:
        # The cwd/path contract also runs a standalone copy of this fixture.
        cpu_affinity = None
    history_length = 0
    for raw in sys.stdin:
        line = raw.strip()
        with open(trace, "a") as output:
            cpus = cpu_affinity.available_cpus() if cpu_affinity else None
            output.write(json.dumps({"command": line, "seed": seed, "pid": os.getpid(),
                                     "affinity": sorted(cpus) if cpus is not None else None}) + "\n")
        if line == "5duci":
            print("info string scripted test peer", flush=True)
            print("5duciok", flush=True)
        elif line == "isready":
            print("readyok", flush=True)
        elif line.startswith("position "):
            history_length = line.partition(" moves ")[2].split().count("submit")
        elif line.startswith("go "):
            if mode == "exit":
                return 7
            if mode == "error":
                print("info string error scripted failure", flush=True)
                continue
            if mode == "timeout":
                time.sleep(10)
                continue
            tokens = line.split()
            count = int(iterations)
            if "depth" in tokens:
                count = 10 * int(tokens[tokens.index("depth") + 1])
            print(f"info mcts_stats iterations={count} elapsed_seconds=0.1 ips={count * 10}", flush=True)
            print("info mcts_score average=0.5 detailed=0.5", flush=True)
            if mode == "nobestmove":
                print("nobestmove", flush=True)
            elif mode == "illegal" or (mode == "illegal-depth" and "depth" in tokens):
                print("bestmove (0T1)d2d8", flush=True)
            else:
                move = "(0T1)d2d3" if history_length == 0 else "(0T1)d7d6"
                print(f"bestmove {move}", flush=True)
        elif line == "quit":
            return 0
    return 0


class ContractCase(unittest.TestCase):
    """Isolation and invocation utilities, without inspecting script internals."""

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="5dchess-api-")
        self.addCleanup(temporary.cleanup)
        # Resolve macOS /var aliases before computing relative CLI paths.
        self.work = Path(temporary.name).resolve()
        self.database = self.work / "ratings.sqlite3"

    def command(self, script, *arguments, cwd=None, expected_code=0):
        result = subprocess.run(
            [sys.executable, str(SCRIPTS / script), *map(str, arguments)],
            cwd=cwd or self.work, stdin=subprocess.DEVNULL,
            capture_output=True, text=True, timeout=45,
        )
        self.assertEqual(result.returncode, expected_code,
                         f"{script} {arguments}\n{result.stdout}\n{result.stderr}")
        return result

    def elo(self, *arguments, expected_code=0):
        return self.command("elo.py", "--database", self.database,
                            *arguments, expected_code=expected_code)

    def elo_json(self, *arguments):
        return json.loads(self.elo(*arguments).stdout)

    def register_pair(self, alpha="unused-alpha", beta="unused-beta"):
        for name, command in (("alpha", alpha), ("beta", beta)):
            self.elo("register", name, "--command", command)

    def require_bindings(self):
        if not list(BUILD.glob("engine*.so")) and not list(BUILD.glob("engine*.pyd")):
            self.skipTest("Build the Python extension with PYMODULE=ON first.")

    def bindings(self):
        self.require_bindings()
        old_path = sys.path[:]
        try:
            sys.path.insert(0, str(BUILD))
            return importlib.import_module("engine")
        finally:
            sys.path[:] = old_path

    def peer(self, label, mode="legal", iterations=100, seed="7"):
        # Controllers explicitly parse command strings with shlex.split.
        return shlex.join([sys.executable, str(THIS_FILE), "--scripted-engine",
                           mode, str(self.work / f"{label}.jsonl"), str(iterations), seed])

    def trace(self, label):
        return [json.loads(line) for line in (self.work / f"{label}.jsonl").read_text().splitlines()]

    @staticmethod
    def csv_rows(path):
        with path.open(newline="") as source:
            return list(csv.DictReader(source))


class CLIContracts(ContractCase):
    """Help works in both supported directories; invalid input is a CLI error."""

    def test_help_from_root_and_scripts(self):
        for cwd in (ROOT, SCRIPTS):
            for script in ("autoplay.py", "elo.py", "elo_matchmaker.py", "ratio_matchmaker.py"):
                with self.subTest(cwd=cwd, script=script):
                    self.assertIn("usage:", self.command(script, "--help", cwd=cwd).stdout)

    def test_invalid_arguments(self):
        for script, arguments in (
            ("autoplay.py", ("--games", "0")),
            ("elo.py", ("report", "1", "invalid")),
            ("elo_matchmaker.py", ("run", "--games", "1", "--jobs", "0")),
            ("ratio_matchmaker.py", ("--target", "unused", "--alpha", "1")),
            ("ratio_matchmaker.py", ("--target", "unused", "--max-pairs", "0")),
            ("ratio_matchmaker.py", ("--target", "unused", "--ratios", "nan")),
            ("ratio_matchmaker.py", ("--target", "unused", "--switch-e", "1")),
            ("ratio_matchmaker.py", ("--target", "unused", "--no-pin", "--core", "0")),
            ("ratio_matchmaker.py", ("--target", "unused", "--stat-method", "fixed-looks",
                                          "--schedule", "adaptive")),
            ("ratio_matchmaker.py", ("--target", "unused", "--zero", "unused")),
            ("ratio_matchmaker.py", ("--target", "unused", "--zero-movetime", "40")),
            ("ratio_matchmaker.py", ("--target", "unused", "--workers", "1")),
            ("ratio_matchmaker.py", ("--target", "unused", "--seed-start", "1")),
            ("ratio_matchmaker.py", ("--target", "engine --seed 7")),
            ("elo_matchmaker.py", ("run", "--games", "1", "--seed", "7")),
        ):
            with self.subTest(script=script):
                result = self.command(script, *arguments, expected_code=2)
                self.assertIn("error:", result.stderr)
                self.assertNotIn("Traceback", result.stderr)


class EloContracts(ContractCase):
    """Registry edits, scheduling, manual results, ratings, and cancellation."""

    def test_registry_metadata_and_enable_disable(self):
        self.register_pair()
        self.elo("update", "alpha", "--name", "Alpha display", "--max-parallel", "2")
        self.elo("clone", "alpha", "gamma", "--command", "unused-gamma")
        self.elo("rename", "gamma", "--name", "Gamma display", "--command", "renamed-command")
        self.elo("disable", "beta")
        rows = {row["id"]: row for row in self.elo_json("list", "--json")}
        self.assertEqual(rows["alpha"]["name"], "Alpha display")
        self.assertEqual(rows["alpha"]["max_parallel"], 2)
        self.assertEqual(rows["gamma"]["command"], "renamed-command")
        self.assertFalse(rows["beta"]["enabled"])
        self.elo("enable", "beta")
        self.assertTrue(all(row["enabled"] for row in self.elo_json("list", "--json")))

    def test_schedule_report_finalize_and_ratings(self):
        self.register_pair()
        self.assertEqual(len(self.elo_json("suggest", "2", "--format", "json")), 2)
        self.assertEqual(self.elo_json("pending", "--json"), [])
        matches = self.elo_json("schedule", "2", "--format", "json")
        self.assertEqual(len(matches), 2)
        self.assertEqual(len(self.elo_json("pending", "--json")), 2)
        game_file = self.work / "reported.5dpgn"
        game_file.write_text(STANDARD_GAME)
        for match in matches:
            result = "white" if match["white_id"] == "alpha" else "black"
            self.elo("report", match["match_id"], result, "--pgn", game_file, "--no-auto-finalize")
        self.elo("finalize", matches[0]["batch_id"])
        self.assertEqual(self.elo_json("pending", "--json"), [])
        history = self.elo_json("history", "--json")
        self.assertEqual(len(history), 2)
        self.assertTrue(all(STANDARD_GAME in row["pgn"] for row in history))
        board = {row["id"]: row for row in self.elo_json("leaderboard", "--json")}
        self.assertEqual(board["alpha"]["wins"], 2)
        self.assertEqual(board["beta"]["losses"], 2)
        self.assertGreater(board["alpha"]["rating"], board["beta"]["rating"])

    def test_abort_and_void_do_not_count_as_rated_games(self):
        self.register_pair()
        matches = self.elo_json("schedule", "2", "--format", "json")
        self.elo("abort", "match", matches[0]["match_id"])
        self.elo("report", matches[1]["match_id"], "void")
        self.assertEqual(self.elo_json("pending", "--json"), [])
        matches = self.elo_json("schedule", "2", "--format", "json")
        self.elo("abort", "batch", matches[0]["batch_id"])
        self.assertEqual(self.elo_json("pending", "--json"), [])
        self.assertTrue(all(row["games"] == 0 for row in self.elo_json("leaderboard", "--json")))


class AutoplayContracts(ContractCase):
    """Protocol/history, PGN, metrics, path semantics, and fault reporting."""

    def play(self, mode="legal", *extra, cwd=None):
        self.require_bindings()
        return self.command(
            "autoplay.py", "--white", self.peer("white", mode),
            "--black", self.peer("black"), "--games", "1", "--movetime", "30",
            "--timeout", "500", "--max-actions", "2", *extra, cwd=cwd,
        )

    def test_legal_game_metadata_metrics_history_and_shutdown(self):
        result = self.play("legal", "--white-name", "Alpha", "--black-name", "Beta",
                           "--event", "API contract", "--site", "Test")
        for header in (
            '[White "Alpha"]', '[Black "Beta"]', '[Event "API contract"]',
            '[Site "Test"]', '[Result "1/2-1/2"]',
        ):
            self.assertIn(header, result.stdout)
        self.assertIn("1/1 games completed", result.stdout)
        self.assertIn("Protocol failures: 0", result.stdout)
        rows = self.csv_rows(self.work / "logs/autoplay-go-metrics.csv")
        self.assertEqual([row["color"] for row in rows], ["white", "black"])
        self.assertTrue(all(row["iterations"] == "100" and row["status"] == "bestmove" for row in rows))
        black_commands = [row["command"] for row in self.trace("black")]
        self.assertTrue(any(" moves (0T1)d2d3 submit" in command for command in black_commands))
        for label in ("white", "black"):
            self.assertEqual(self.trace(label)[-1]["command"], "quit")

    def test_explicit_module_and_engine_paths_are_relative_to_cwd(self):
        self.require_bindings()
        # Copy the protocol fixture under cwd, then invoke it by relative path.
        fixture = self.work / "peer.py"
        fixture.write_text(THIS_FILE.read_text())
        peer = shlex.join([sys.executable, "peer.py", "--scripted-engine", "legal", "trace.jsonl", "100", "7"])
        result = self.command("autoplay.py", "--white", peer, "--black", peer,
                              "--module-dir", os.path.relpath(BUILD, self.work),
                              "--games", "1", "--movetime", "30", "--max-actions", "2")
        self.assertIn("Protocol failures: 0", result.stdout)
        self.assertTrue((self.work / "logs/autoplay-go-metrics.csv").is_file())

    def test_illegal_move_and_no_move_forfeit(self):
        for mode in ("illegal", "nobestmove"):
            with self.subTest(mode=mode):
                result = self.play(mode)
                self.assertIn("Black wins: 1", result.stdout)
                self.assertIn('[Result "0-1"]', result.stdout)
                if mode == "illegal":
                    diagnostics = list((self.work / "logs").glob("illegal-action-*.txt"))
                    self.assertTrue(diagnostics)
                    self.assertIn("Reported bestmove:", diagnostics[0].read_text())
                else:
                    self.assertTrue(list((self.work / "logs").glob("adjudication-*.5dpgn")))

    def test_protocol_exit_error_and_timeout_keep_diagnostics(self):
        for mode in ("exit", "error", "timeout"):
            with self.subTest(mode=mode):
                result = self.play(mode)
                self.assertIn("Protocol failures: 1", result.stdout)
                self.assertIn('[Result "*"]', result.stdout)
                self.assertIn("protocol failure", result.stdout + result.stderr)
                rows = self.csv_rows(self.work / "logs/autoplay-go-metrics.csv")
                self.assertEqual(rows[0]["status"], "protocol_error")
                diagnostics = list((self.work / "logs").glob("protocol-failure-*.txt"))
                self.assertTrue(diagnostics)
                self.assertIn("Partial game PGN:", diagnostics[0].read_text())


class MatchmakerContracts(ContractCase):
    """Run/resume durable queues, persisted artifacts, and failure recovery."""

    def run_queue(self, action, *arguments, expected_code=0):
        self.require_bindings()
        return self.command("elo_matchmaker.py", "--database", self.database,
                            action, *arguments, "--movetime", "30", "--timeout", "500",
                            "--max-actions", "2", "--logs-dir", "match-logs",
                            expected_code=expected_code)

    def test_run_and_resume_save_draws_and_game_artifacts(self):
        self.require_bindings()
        self.register_pair(self.peer("alpha"), self.peer("beta"))
        self.run_queue("run", "--games", "2", "--jobs", "2")
        self.assertEqual(self.elo_json("pending", "--json"), [])
        self.elo("schedule", "1")
        self.run_queue("resume", "--jobs", "1")
        self.assertEqual(self.elo_json("pending", "--json"), [])
        history = self.elo_json("history", "--json")
        self.assertEqual(len(history), 3)
        # Matchmaker's visible policy records action-capped games as draws.
        self.assertTrue(all(row["result"] == "draw" for row in history))
        for row in history:
            self.assertIn('[Result "1/2-1/2"]', row["pgn"])
            self.assertTrue(Path(row["output_log"]).is_file())
            self.assertEqual(len(self.csv_rows(Path(row["metrics_csv"]))), 2)
        self.assertEqual({row["draws"] for row in self.elo_json("leaderboard", "--json")}, {3})

    def test_default_pinning_unique_seeds_opening_and_exact_output_directory(self):
        self.require_bindings()
        self.register_pair(self.peer("alpha", seed="{seed}"), self.peer("beta", seed="{seed}"))
        result = self.command(
            "elo_matchmaker.py", "--database", self.database, "run", "--games", "2", "-j", "2",
            "--matchmaking-seed", "23", "--output-dir", "exact-run",
            "--opening-text", STANDARD_GAME, "--event", "Shared CLI",
            "--movetime", "30", "--max-actions", "2",
        )
        self.assertIn("jobs=2", result.stdout)
        launches = [json.loads(line) for line in (self.work / "exact-run/engine-launches.jsonl").read_text().splitlines()]
        self.assertEqual(len(launches), 4)
        self.assertEqual(len({row["seed"] for row in launches}), 4)
        rows = self.trace("alpha") + self.trace("beta")
        self.assertEqual({row["seed"] for row in rows}, {str(row["seed"]) for row in launches})
        if all(row["affinity"] is not None for row in rows):
            self.assertTrue(all(len(row["affinity"]) == 1 for row in rows))
        history = self.elo_json("history", "--json")
        self.assertEqual(len(history), 2)
        self.assertTrue(all('[Event "Shared CLI"]' in row["pgn"] for row in history))

    def test_protocol_failure_pauses_then_disabled_engine_is_aborted(self):
        self.require_bindings()
        self.register_pair(self.peer("alpha", "error"), self.peer("beta", "error"))
        result = self.run_queue("run", "--games", "2", "--jobs", "1", expected_code=2)
        self.assertIn("paused after match", result.stderr)
        pending = self.elo_json("pending", "--json")
        self.assertTrue(any(row["status"] == "scheduled" for row in pending))
        self.assertFalse(any(row["status"] == "running" for row in pending))
        # Queued commands are immutable snapshots. Follow the documented
        # recovery flow: disable the bad engine and resume to discard its queue.
        self.elo("disable", "alpha")
        self.run_queue("resume", "--jobs", "1")
        self.assertEqual(self.elo_json("pending", "--json"), [])
        history = self.elo_json("history", "--json")
        self.assertEqual(len(history), 2)
        self.assertTrue(any(row["result"] == "void" for row in history))
        self.assertTrue(any(row["status"] == "aborted" for row in history))

    def test_rejected_fixed_seed_queue_can_be_recovered_by_disabling_engine(self):
        self.require_bindings()
        self.register_pair(self.peer("alpha") + " --seed 7", self.peer("beta"))
        result = self.run_queue("run", "--games", "2", expected_code=2)
        self.assertIn("fixed engine seeds", result.stderr)
        self.assertEqual(len(self.elo_json("pending", "--json")), 2)
        self.elo("disable", "alpha")
        self.run_queue("resume")
        self.assertEqual(self.elo_json("pending", "--json"), [])
        self.assertTrue(all(row["status"] == "aborted" for row in self.elo_json("history", "--json")))


class RatioContracts(ContractCase):
    """Known metrics produce observable budgets; pairs/logs/summaries agree."""

    def run_ratio(self, mode="legal", method="anytime"):
        self.require_bindings()
        shutil.rmtree(self.work / "ratio-output", ignore_errors=True)
        for label in ("target", "baseline"):
            (self.work / f"{label}.jsonl").unlink(missing_ok=True)
        return self.command(
            "ratio_matchmaker.py", "--target", self.peer("target", mode, 200, "{seed}"),
            "--baseline", self.peer("baseline", "legal", 100, "{seed}"),
            "--ratios", ".5", "--max-pairs", "1", "--jobs", "2",
            "--baseline-movetime", "40", "--max-target-movetime", "200",
            "--max-actions", "2", "--output-dir", "ratio-output",
            "--stat-method", method, "--look-pairs", "1", "--event", "Ratio contract",
        )

    def test_budgets_color_reversal_and_output_files(self):
        for method in ("anytime", "fixed-looks"):
            with self.subTest(method=method):
                result = self.run_ratio(method=method)
                self.assertIn("Iteration-ratio summary", result.stdout)
                self.assertLess(result.stdout.index("Output guide:"), result.stdout.index("Calibration CPU"))
                self.assertIn("wins-draws-losses", result.stdout)
                self.assertIn("target/baseline IPS", result.stdout)
                self.assertIn("Baseline:", result.stdout)
                self.assertIn("Most aggressive established ratio: none", result.stdout)
                output = self.work / "ratio-output"
                games = self.csv_rows(output / "games.csv")
                self.assertEqual(len(games), 2)
                self.assertEqual({row["target_color"] for row in games}, {"white", "black"})
                self.assertEqual(len({row["cpu"] for row in games}), 1)
                self.assertEqual({row["outcome"] for row in games}, {"draw"})
                self.assertTrue(all(row["reason"] == "action limit reached" for row in games))
                for row in games:
                    self.assertIn('[Result "1/2-1/2"]', (self.work / row["pgn_path"]).read_text())
                    self.assertIn('"Baseline"', (self.work / row["pgn_path"]).read_text())
                    self.assertIn('[Event "Ratio contract"]', (self.work / row["pgn_path"]).read_text())
                searches = self.csv_rows(output / "searches.csv")
                target = [row for row in searches if row["role"] == "target"]
                self.assertEqual(len(target), 2)
                for row in target:
                    self.assertEqual(row["reference_iterations"], "100")
                    self.assertEqual(row["requested_iterations"], "50")
                    self.assertEqual(row["iteration_limit"], "50")
                    self.assertEqual(row["actual_iterations"], "50")
                    self.assertEqual(row["time_capped"], "0")
                requests = self.trace("target")
                launches = [json.loads(line) for line in (output / "engine-launches.jsonl").read_text().splitlines()]
                seeds = [row["seed"] for row in launches]
                self.assertEqual(len(seeds), len(set(seeds)))
                self.assertTrue(all(isinstance(seed, int) and 0 <= seed < 2**32 for seed in seeds))
                self.assertEqual({row["seed"] for row in requests},
                                 {str(row["seed"]) for row in launches if row["role"] == "target"})
                self.assertTrue(any("depth 5" in row["command"] for row in requests))
                # Check actual child masks, including meter and calibration
                # processes, against the CPU recorded for each game.
                if all(row["affinity"] is not None for row in requests):
                    peer_rows = requests + self.trace("baseline")
                    for row in peer_rows:
                        self.assertEqual(len(row["affinity"]), 1)
                    self.assertEqual({int(row["cpu"]) for row in games},
                                     {row["affinity"][0] for row in requests
                                      if row["command"].startswith("go ") and "depth" in row["command"]})

    def test_adaptive_cli_descends_before_certification(self):
        self.require_bindings()
        # Deliberate baseline illegal actions give known wins in both colors.
        # Small exploratory thresholds keep this a controller contract test.
        result = self.command(
            "ratio_matchmaker.py", "--target", self.peer("target", seed="{seed}"),
            "--baseline", self.peer("baseline", "illegal", seed="{seed}"),
            "--ratios", "1,.5", "--max-pairs", "20", "--jobs", "2",
            "--baseline-movetime", "40", "--max-target-movetime", "200",
            "--max-actions", "2", "--output-dir", "adaptive-output",
            "--switch-e", "1.1", "--switch-min-pairs", "1", "--alpha", ".9",
        )
        self.assertIn("Provisional descent", result.stdout)
        self.assertLess(result.stdout.index("Provisional descent"), result.stdout.index("decision: noninferior"))
        self.assertIn("Most aggressive established ratio: m=0.5", result.stdout)
        games = self.csv_rows(self.work / "adaptive-output/games.csv")
        self.assertEqual({row["ratio"] for row in games}, {"1.0", "0.5"})
        self.assertEqual({row["outcome"] for row in games}, {"win"})
        launches = [json.loads(line) for line in (self.work / "adaptive-output/engine-launches.jsonl").read_text().splitlines()]
        self.assertEqual(len(launches), len({row["seed"] for row in launches}))

    def test_known_target_failures_are_losses_in_both_colors(self):
        result = self.run_ratio("illegal-depth")
        games = self.csv_rows(self.work / "ratio-output/games.csv")
        self.assertEqual(len(games), 2)
        self.assertEqual({row["outcome"] for row in games}, {"loss"})
        self.assertTrue(all("target illegal action" in row["reason"] for row in games))
        self.assertIn("games 0-0-2", result.stdout)
        self.assertIn("mean score=0", result.stdout)


class BindingContracts(ContractCase):
    """Real public engine objects: legal/illegal moves, history, and PGN."""

    def test_submission_policy(self):
        rules = self.bindings()
        game = rules.game.from_pgn(STANDARD_GAME + " 1. f3 / e5 2. g4 / Qh4")
        self.assertFalse(game.can_submit())
        self.assertFalse(game.can_submit(allow_submit_with_checks=True))
        self.assertFalse(game.submit(allow_submit_with_checks=True))
        self.assertTrue(game.apply_move(rules.ext_move(rules.vec4(0, 1, 3, 0), rules.vec4(0, 2, 3, 0), rules.Piece.NO_PIECE)))
        draft = game.get_current_fen()
        self.assertFalse(game.can_submit())
        self.assertTrue(game.can_submit(allow_submit_with_checks=True))
        self.assertFalse(game.submit())
        self.assertEqual(game.get_current_fen(), draft)
        self.assertFalse(game.has_illegal_history())
        self.assertTrue(game.submit(allow_submit_with_checks=True))
        self.assertTrue(game.has_illegal_history())


    def test_setup_header_failures_are_value_errors(self):
        rules = self.bindings()
        for pgn, message in [
            ('[Board "Unknown"]', 'Unknown variant'),
            ('[Size "ax8"]', 'Invalid Size value'),
            ('[Timeline "sideways"]', 'Invalid Timeline value'),
            ('[Promotions "*Q"]', 'Invalid Promotions value'),
            ('[Event "No setup"]', 'Missing initial setup'),
        ]:
            with self.subTest(pgn=pgn):
                for allow in (False, True):
                    with self.assertRaisesRegex(ValueError, message):
                        rules.game.from_pgn(pgn, allow_illegal_history=allow)

    def test_illegal_history_requires_opt_in_and_survives_navigation(self):
        rules = self.bindings()
        pgn = STANDARD_GAME + '\n1. f3 / e5 2. g4 / Qh4 3. a3 / a6'
        with self.assertRaisesRegex(ValueError, 'Illegal action'):
            rules.game.from_pgn(pgn)
        game = rules.game.from_pgn(pgn, allow_illegal_history=True)
        self.assertTrue(game.has_illegal_history())
        self.assertIsNone(game.suggest_action())
        self.assertEqual(game.get_match_status(), rules.match_status_t.ILLEGAL_HISTORY)
        exported = game.show_pgn(rules.SHOW_MATE | rules.SHOW_OUTCOME)
        restored = rules.game.from_pgn(exported, allow_illegal_history=True)
        self.assertTrue(restored.has_illegal_history())
        self.assertEqual(game.get_current_fen(), restored.get_current_fen())
        game.visit_parent()
        game.visit_parent()
        self.assertFalse(game.has_illegal_history())
        summary = game.get_child_actions()[0]
        action = summary.act
        self.assertTrue(summary.is_illegal)
        self.assertEqual(game.get_match_status(), rules.match_status_t.BLACK_WINS)
        self.assertTrue(game.visit_child(action))
        self.assertTrue(game.has_illegal_history())

    def test_metadata_edits_are_live_and_assignment_preserves_views(self):
        rules = self.bindings()
        game = rules.game.from_pgn(STANDARD_GAME)
        position = game.get_current_fen()
        metadata = game.metadata
        metadata["event"] = "Live edits"
        self.assertEqual(game.metadata["event"], "Live edits")
        self.assertIn('[Event "Live edits"]', game.show_pgn(rules.SHOW_NOTHING))
        metadata.update({"white": "Alice", "black": "Bob"})
        self.assertEqual(metadata.get("white"), "Alice")
        self.assertIsNone(metadata.get("missing"))
        self.assertEqual(metadata.get("missing", "fallback"), "fallback")
        snapshot = dict(metadata)
        snapshot["event"] = "Independent copy"
        self.assertEqual(metadata["event"], "Live edits")
        del metadata["event"]
        self.assertNotIn("event", game.metadata)
        with self.assertRaises(KeyError):
            metadata["event"]
        with self.assertRaises(KeyError):
            del metadata["event"]
        # Materialize an independent dictionary before replacing live data.
        old_headers = dict(metadata)
        game.metadata = {"event": "Replacement"}
        self.assertEqual(old_headers["white"], "Alice")
        self.assertEqual(dict(metadata), {"event": "Replacement"})
        self.assertEqual(game.get_current_fen(), position)
        with self.assertRaises(TypeError):
            game.metadata = {"event": 42}
        self.assertEqual(dict(metadata), {"event": "Replacement"})
        self.assertEqual(dict(rules.Metadata({"event": "Standalone"})), {"event": "Standalone"})
        # A retained mapping owns the game even after its Python name is gone.
        del game
        metadata["event"] = "Still alive"
        self.assertEqual(metadata.copy(), {"event": "Still alive"})
        self.assertEqual(list(metadata.items()), [("event", "Still alive")])
        self.assertEqual(list(metadata.values()), ["Still alive"])
        metadata.clear()
        self.assertEqual(len(metadata), 0)

    def test_endpoint_hash_ignores_metadata_and_tracks_navigation(self):
        rules = self.bindings()
        initial = rules.game.from_pgn(STANDARD_GAME)
        game = rules.game.from_pgn(STANDARD_GAME + '\n[Result "0-1"]\n1. e4 {clock}')
        endpoint = game.get_current_hash()
        self.assertNotEqual(endpoint, initial.get_current_hash())
        self.assertEqual(endpoint, rules.game.from_pgn(STANDARD_GAME + '\n1. e4').get_current_hash())
        game.visit_parent()
        self.assertEqual(game.get_current_hash(), initial.get_current_hash())

    def test_duplicate_pgn_actions_replace_comments_and_descendants(self):
        rules = self.bindings()
        game = rules.game.from_pgn(STANDARD_GAME + """
(1. Nf3 {old} / Nf6 2. Nc3 {discarded})
1. Ng1f3 {replacement}
""")
        self.assertEqual(game.get_comments(), ["replacement"])
        expected = rules.game.from_pgn(STANDARD_GAME + '\n1. Nf3')
        self.assertEqual(game.get_current_hash(), expected.get_current_hash())
        self.assertEqual(game.get_child_actions(), [])
        game.visit_parent()
        children = game.get_child_actions()
        self.assertEqual(len(children), 1)
        self.assertTrue(game.has_child(children[0].act))
        self.assertFalse(game.add_child(children[0].act))
        self.assertTrue(game.visit_child(children[0].act))
        self.assertEqual(game.get_comments(), ["replacement"])
        exported = game.show_pgn(rules.SHOW_CAPTURE | rules.SHOW_PROMOTION)
        self.assertNotIn("discarded", exported)
        self.assertNotIn("{old}", exported)

    def test_child_membership_insertion_and_existing_only_navigation(self):
        rules = self.bindings()
        game = rules.game.from_pgn(STANDARD_GAME)
        initial = game.get_current_hash()
        action = game.suggest_action()
        self.assertIsNotNone(action)
        self.assertFalse(game.has_child(action))
        self.assertFalse(game.visit_child(action))
        self.assertEqual(game.get_current_hash(), initial)
        self.assertEqual(game.get_child_actions(), [])
        self.assertTrue(game.add_child(action))
        self.assertTrue(game.has_child(action))
        self.assertFalse(game.add_child(action))
        self.assertEqual(game.get_current_hash(), initial)
        self.assertEqual(len(game.get_child_actions()), 1)
        self.assertTrue(game.visit_child(action))
        game.set_comments(["keep"])
        game.visit_parent()
        self.assertTrue(game.visit_child(action))
        self.assertEqual(game.get_comments(), ["keep"])
        game.visit_parent()
        self.assertTrue(game.delete_child(action))
        self.assertFalse(game.has_child(action))
        self.assertFalse(game.visit_child(action))
        self.assertEqual(game.get_child_actions(), [])

    def test_child_action_notation_flags_preserve_actions_and_position(self):
        rules = self.bindings()
        game = rules.game.from_pgn(STANDARD_GAME + '\n1. e4')
        game.visit_parent()
        position = game.get_current_hash()
        flags = rules.SHOW_CAPTURE | rules.SHOW_PROMOTION
        default = game.get_child_actions()
        full = game.get_child_actions(flags)
        short = game.get_child_actions(flags | rules.SHOW_SHORT)
        self.assertTrue(all(not summary.is_illegal for summary in default))
        self.assertEqual([summary.pgn for summary in default], [summary.pgn for summary in full])
        self.assertEqual([summary.pgn for summary in short], ['e4'])
        self.assertNotEqual(full[0].pgn, short[0].pgn)
        self.assertEqual(full[0].act, short[0].act)
        self.assertEqual(game.get_current_hash(), position)
        game.visit_child(short[0].act)
        self.assertNotEqual(game.get_current_hash(), position)

    def test_suggestions_stay_private_and_hint_insertion_preserves_draft_and_redo(self):
        rules = self.bindings()
        game = rules.game.from_pgn(STANDARD_GAME)
        witness = game.suggest_action()
        self.assertIsNotNone(witness)
        self.assertEqual(game.get_child_actions(), [])
        self.assertEqual(game.suggest_action(), witness)
        self.assertEqual(game.get_match_status(), rules.match_status_t.PLAYING)
        self.assertTrue(game.apply_move(rules.ext_move(
            rules.vec4(4, 1, 1, 0), rules.vec4(4, 2, 1, 0), rules.NO_PIECE)))
        draft = game.get_current_fen()
        self.assertTrue(game.add_child(witness))
        self.assertFalse(game.add_child(witness))
        self.assertEqual(game.get_current_fen(), draft)
        self.assertTrue(game.undo())
        self.assertTrue(game.can_redo())
        next_witness = game.suggest_action()
        self.assertIsNotNone(next_witness)
        self.assertNotEqual(witness, next_witness)
        self.assertTrue(game.add_child(next_witness))
        self.assertTrue(game.can_redo())
        self.assertTrue(game.redo())
        self.assertEqual(game.get_current_fen(), draft)

    def test_hint_exhaustion_is_not_terminal_and_navigation_resets_status(self):
        rules = self.bindings()
        game = rules.game.from_pgn(STANDARD_GAME)
        for _ in range(100):
            witness = game.suggest_action()
            if witness is None:
                break
            self.assertTrue(game.add_child(witness))
        else:
            self.fail('opening suggestions did not exhaust')
        self.assertGreater(len(game.get_child_actions()), 0)
        self.assertIsNone(game.suggest_action())
        self.assertEqual(game.get_match_status(), rules.match_status_t.PLAYING)
        terminal = rules.game.from_pgn(STANDARD_GAME + '\n1. f3 / e5 2. g4 / Qh4')
        self.assertIsNone(terminal.suggest_action())
        self.assertEqual(terminal.get_match_status(), rules.match_status_t.BLACK_WINS)
        terminal.visit_parent()
        self.assertIsNotNone(terminal.suggest_action())
        self.assertEqual(terminal.get_match_status(), rules.match_status_t.PLAYING)
        stale = rules.game.from_pgn('[Size "4x4"]\n[Board "Custom"]\n[k*3/4/1q2/3K*:0:1:w]')
        self.assertIsNone(stale.suggest_action())
        self.assertEqual(stale.get_match_status(), rules.match_status_t.STALEMATE)

    def test_coordinate_constructor_rejects_unrepresentable_components(self):
        rules = self.bindings()
        for index, (name, lower, upper) in enumerate((
                ("x", -8, 7), ("y", -8, 7),
                ("t", -2048, 2047), ("l", -2048, 2047))):
            for value in (lower, upper):
                components = [0, 0, 0, 0]
                components[index] = value
                with self.subTest(component=name, value=value):
                    point = rules.vec4(**dict(zip(("x", "y", "t", "l"), components)))
                    self.assertEqual((point.x(), point.y(), point.t(), point.l()), tuple(components))
            for value in (lower - 1, upper + 1, -2147483648, 2147483647, -(1 << 100), 1 << 100):
                components = [0, 0, 0, 0]
                components[index] = value
                with self.subTest(component=name, value=value):
                    with self.assertRaisesRegex(ValueError, name):
                        rules.vec4(*components)
        self.assertEqual((rules.vec4(7, 0, 0, 0) + rules.vec4(1, 0, 0, 0)).x(), -8)

    def test_coordinates_and_move_values(self):
        rules = self.bindings()
        point = rules.vec4(3, 1, 1, 0)
        self.assertEqual((point.x(), point.y(), point.t(), point.l()), (3, 1, 1, 0))
        destination = point + rules.vec4(0, 1, 0, 0)
        move = rules.ext_move(point, destination, rules.Piece.NO_PIECE)
        self.assertEqual(move.get_from(), point)
        self.assertEqual(move.get_to(), destination)
        self.assertEqual(move.get_promote(), rules.Piece.NO_PIECE)
        self.assertEqual(move.to_string(), "(0T1)d2d3")
        self.assertTrue(rules.get_version())

    def test_game_move_undo_redo_submit_history_and_pgn_round_trip(self):
        rules = self.bindings()
        game = rules.game.from_pgn(STANDARD_GAME)
        self.assertFalse(hasattr(game, "get_current_state"))
        initial = game.get_current_fen()
        source, target = rules.vec4(3, 1, 1, 0), rules.vec4(3, 2, 1, 0)
        self.assertEqual(game.get_board_size(), (8, 8))
        self.assertEqual(game.get_match_status(), rules.match_status_t.PLAYING)
        self.assertIn(source, game.get_movable_pieces())
        self.assertIn(target, game.gen_move_if_playable(source))
        self.assertFalse(game.apply_move(rules.ext_move(source, rules.vec4(3, 7, 1, 0), rules.NO_PIECE)))
        self.assertEqual(game.get_current_fen(), initial)
        self.assertTrue(game.apply_move(rules.ext_move(source, target, rules.NO_PIECE)))
        moved = game.get_current_fen()
        self.assertNotEqual(moved, initial)
        self.assertTrue(game.can_submit())
        self.assertTrue(game.undo())
        self.assertEqual(game.get_current_fen(), initial)
        self.assertTrue(game.can_redo())
        self.assertTrue(game.redo())
        self.assertEqual(game.get_current_fen(), moved)
        self.assertTrue(game.submit())
        submitted = game.get_current_fen()
        self.assertEqual(game.get_current_present(), (1, True))
        self.assertTrue(game.has_parent())
        game.visit_parent()
        self.assertEqual(game.get_current_fen(), initial)
        children = game.get_child_actions()
        self.assertEqual(len(children), 1)
        self.assertEqual(len(children[0].act.get_moves()), 1)
        game.visit_child(children[0].act)
        self.assertEqual(game.get_current_fen(), submitted)
        game.set_comments(["Public API round trip"])
        self.assertIn("Public API round trip", game.get_comments())
        pgn = game.show_pgn(rules.SHOW_ALL & ~rules.SHOW_RELATIVE)
        restored = rules.game.from_pgn(pgn)
        self.assertEqual(restored.get_current_fen(), submitted)
        self.assertEqual(restored.get_current_present(), game.get_current_present())


class HostContracts(ContractCase):
    """HTTP and Socket.IO scenarios run in a fresh server instance subprocess."""

    def test_page_moves_load_export_and_navigation(self):
        if not (ROOT / "extern/client/templates/index.html").is_file():
            self.skipTest(
                "Initialize extern/client with git submodule update --init --recursive extern/client."
            )
        self.require_bindings()
        for dependency in ("flask", "flask_socketio"):
            if importlib.util.find_spec(dependency) is None:
                self.skipTest(f"Install {dependency} for the web contract checks.")
        result = subprocess.run([sys.executable, str(THIS_FILE), "--host-contract"],
                                cwd=self.work, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


def host_contract():
    """Only server wiring is imported; all game actions go through messages."""
    class Scenario(unittest.TestCase):
        def runTest(self):
            sys.path.insert(0, str(SCRIPTS))
            import host
            page = host.app.test_client().get("/")
            self.assertEqual(page.status_code, 200)
            self.assertIn(b"<html", page.data.lower())
            client = host.socketio.test_client(host.app)
            self.addCleanup(client.disconnect)
            self.assertTrue(client.is_connected())

            def data(event, *arguments):
                client.emit(event, *arguments)
                packets = client.get_received()
                responses = [packet["args"][0] for packet in packets if packet["name"] == "response_data"]
                self.assertTrue(responses, f"{event} returned no board data: {packets}")
                return responses[-1]

            initial = data("request_load", STANDARD_GAME)
            self.assertEqual(initial["size"], {"x": 8, "y": 8})
            self.assertEqual(data("request_data", {})["boards"], initial["boards"])
            data("click", dict(l=0, t=1, c=0, x=3, y=1))
            moved = data("click", dict(l=0, t=1, c=0, x=3, y=2))
            self.assertNotEqual(moved["boards"], initial["boards"])
            self.assertEqual(moved["submit-button"], "enabled")
            undone = data("request_undo")
            self.assertEqual(undone["boards"], initial["boards"])
            self.assertEqual(undone["redo-button"], "enabled")
            self.assertEqual(data("request_redo")["boards"], moved["boards"])
            submitted = data("request_submit")
            self.assertEqual(submitted["match-status"], "Black's move")
            self.assertEqual(submitted["prev-button"], "enabled")
            parent = data("request_prev")
            self.assertEqual(parent["boards"], initial["boards"])
            self.assertTrue(parent["next-options"])
            child = data("request_next", next(iter(parent["next-options"])))
            self.assertEqual(child["boards"], submitted["boards"])
            self.assertEqual(data("right_click", {})["boards"], submitted["boards"])
            client.emit("request_pgn")
            pgn = next(packet["args"][0] for packet in client.get_received() if packet["name"] == "response_pgn")
            self.assertEqual(data("request_load", pgn)["boards"], submitted["boards"])
            small = data("request_load", SMALL_GAME)
            self.assertEqual(small["size"], {"x": 4, "y": 4})
            # Hint is a rules action, not a search-quality assertion.
            hinted = data("request_hint")
            self.assertEqual(hinted["boards"], small["boards"])
            self.assertTrue(hinted["next-options"])
            suggestion = data("request_next", next(iter(hinted["next-options"])))
            self.assertNotEqual(suggestion["boards"], small["boards"])

    return 0 if unittest.TextTestRunner(verbosity=2).run(Scenario()).wasSuccessful() else 1


class RealEngineSmoke(ContractCase):
    """Real search must communicate and play legal actions; outputs may vary."""

    def test_short_real_engine_game(self):
        self.require_bindings()
        executable = BUILD / ("5dchess.exe" if os.name == "nt" else "5dchess")
        if not executable.is_file():
            self.skipTest("Build the 5dchess executable for the search smoke test.")
        command = shlex.join([str(executable), "zero"])
        result = self.command("autoplay.py", "--white", command, "--black", command,
                              "--games", "1", "--movetime", "40", "--max-actions", "2")
        self.assertIn("1/1 games completed", result.stdout)
        self.assertIn("Protocol failures: 0", result.stdout)
        self.assertNotIn("Rejected bestmove", result.stderr)
        self.assertIn("Adjudication errors: 0", result.stdout)
        rows = self.csv_rows(self.work / "logs/autoplay-go-metrics.csv")
        # Two plies of the standard opening cannot be terminal. Their exact
        # moves may vary, but both must be legal bestmove responses.
        self.assertEqual(len(rows), 2)
        self.assertTrue(all(row["status"] == "bestmove" for row in rows))


if __name__ == "__main__":
    if sys.argv[1:2] == ["--scripted-engine"]:
        raise SystemExit(scripted_engine(sys.argv[2:]))
    if sys.argv[1:] == ["--host-contract"]:
        raise SystemExit(host_contract())
    unittest.main()

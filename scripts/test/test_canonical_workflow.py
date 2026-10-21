"""Contracts for the frozen suite and its experiment runner.

Manifest checks need only Python. The real-engine smoke test needs the
engine-testing CMake preset; it plays eight two-action games, not a tournament.
"""

from collections import Counter
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/canonical_test.py"
SUITE = ROOT / "test/engine_suite/v1"
BUILD = Path(os.environ.get("CANONICAL_BUILD_DIR", ROOT / "build-canonical"))


class CanonicalWorkflow(unittest.TestCase):
    def invoke(self, *args, code=0):
        result = subprocess.run([sys.executable, str(SCRIPT), *map(str, args)],
            capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return result

    def test_frozen_suite_and_nested_color_balances(self):
        self.invoke("verify")
        m = json.loads((SUITE / "manifest.json").read_text())
        positions = {p["id"]: p for p in m["positions"]}
        for name, size in (("quick", 4), ("standard", 8), ("confirm", 16), ("pool", 64)):
            rows = [positions[i] for i in m["subsets"][name]]
            self.assertEqual(len(rows), size)
            self.assertEqual(Counter(p["side_to_move"] for p in rows),
                {"white": size // 2, "black": size // 2})
        standard = [positions[i] for i in m["subsets"]["standard"]]
        self.assertIn(1, {p["playable"] for p in standard})
        self.assertGreaterEqual(max(p["playable"] for p in standard), 5)
        self.assertGreaterEqual(max(p["board_count"] for p in standard), 100)

    def test_changed_snapshot_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            suite = Path(folder) / "suite"
            shutil.copytree(SUITE, suite)
            first = suite / "positions/p01.5dpgn"
            first.write_text(first.read_text() + "\n")
            result = self.invoke("verify", "--suite", suite, code=2)
            self.assertIn("content changed: p01", result.stderr)

    def test_changed_source_identity_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            suite = Path(folder) / "suite"
            shutil.copytree(SUITE, suite)
            m = json.loads((suite / "manifest.json").read_text())
            m["positions"][0]["source_sha256"] = "0" * 64
            (suite / "manifest.json").write_text(json.dumps(m))
            self.assertIn("content changed", self.invoke("verify", "--suite", suite, code=2).stderr)

    def test_real_runner_and_extension_keep_reference_and_positions(self):
        binary = BUILD / ("5dchess.exe" if os.name == "nt" else "5dchess")
        if not binary.exists() or not (list(BUILD.glob("engine*.so")) + list(BUILD.glob("engine*.pyd"))):
            self.skipTest("build the engine-testing preset for the real runner smoke test")
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            reference = work / "reference"
            self.invoke("freeze", "--build", BUILD, "--reference", reference)
            self.invoke("verify", "--native", "--build", BUILD)
            output = work / "run"
            command = ("run", "--build", BUILD, "--reference", reference,
                "--subset", "standard", "--skip-first", "4", "--movetime", "10",
                "--target-cap", "300", "--max-actions", "2", "--output", output)
            self.invoke(*command)
            run = json.loads((output / "run.json").read_text())
            self.assertEqual([r["id"] for r in run["positions"]], ["p05", "p06", "p07", "p08"])
            summary = json.loads((output / "summary.json").read_text())
            self.assertEqual(summary["games"], 8)
            self.assertEqual(summary["by_ratio"]["1.0"]["failures"], 0)
            self.assertEqual(summary["by_ratio"]["1.0"]["target_time_capped_searches"], 0)
            self.invoke(*command, code=2)  # Existing evidence cannot be overwritten.
            binary = reference / binary.name
            with binary.open("ab") as stream:
                stream.write(b"changed")
            command = (*command[:-1], work / "tampered")
            self.assertIn("reference binary changed", self.invoke(*command, code=2).stderr)

    def test_custom_reference_engine_is_frozen_and_used(self):
        binary = BUILD / ("5dchess.exe" if os.name == "nt" else "5dchess")
        if not binary.exists() or not (list(BUILD.glob("engine*.so")) + list(BUILD.glob("engine*.pyd"))):
            self.skipTest("build the engine-testing preset for the custom reference test")
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            reference = work / "reference"
            engine = "mvvlva --pw-constant 2 --pw-alpha 0.5"
            self.invoke("freeze", "--build", BUILD, "--reference", reference,
                "--engine", engine)
            self.assertEqual(json.loads((reference / "reference.json").read_text())["engine"], engine)
            output = work / "run"
            self.invoke("run", "--build", BUILD, "--reference", reference,
                "--engine", engine, "--subset", "quick", "--movetime", "10",
                "--target-cap", "300", "--max-actions", "2", "--output", output)
            run = json.loads((output / "run.json").read_text())
            self.assertEqual(run["reference"]["engine"], engine)
            self.assertIn(engine, run["command"][run["command"].index("--zero") + 1])
            summary = json.loads((output / "summary.json").read_text())
            self.assertEqual(summary["games"], 8)
            self.assertEqual(summary["by_ratio"]["1.0"]["failures"], 0)


if __name__ == "__main__":
    unittest.main()

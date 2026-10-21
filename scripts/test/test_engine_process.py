"""Engine launch errors and the two startup handshake barriers."""

import asyncio
from pathlib import Path
import shlex
import sys
import tempfile
import unittest
from unittest.mock import AsyncMock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import autoplay


PEER = r'''
import sys
mode = sys.argv[1]
if mode == "unknown-engine":
    print("Unknown engine: ghost", file=sys.stderr, flush=True)
    for i in range(50):
        print(f"usage line {i}", file=sys.stderr, flush=True)
    sys.exit(2)
if mode == "not-an-engine":
    print("Usage: this program is not a 5DUCI engine", flush=True)
    sys.exit(0)
for raw in sys.stdin:
    command = raw.strip()
    if command == "5duci" and mode != "no-5duciok":
        print("5duciok", flush=True)
    elif command == "isready":
        if mode == "ready-exit":
            print("Cannot initialize engine backend", file=sys.stderr, flush=True)
            sys.exit(3)
        if mode != "no-readyok":
            print("readyok", flush=True)
    elif command == "quit":
        sys.exit(0)
'''


class EngineStartupTests(unittest.IsolatedAsyncioTestCase):
    def engine(self, mode):
        command = shlex.join([sys.executable, "-u", "-c", PEER, mode])
        player = autoplay.EngineProcess("target", command, 500)
        self.addAsyncCleanup(player.close)
        return player

    async def test_missing_executable_is_identified_before_affinity_launcher(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = str(Path(directory) / "missing-engine")
            command = shlex.join([executable, "ghost"])
            for cpu in (None, 42):
                with self.subTest(cpu=cpu):
                    player = autoplay.EngineProcess("target", command, 500, cpu=cpu)
                    with patch.object(asyncio, "create_subprocess_exec", new_callable=AsyncMock) as spawn:
                        with self.assertRaises(autoplay.ProtocolError) as caught:
                            await player.start()
                        spawn.assert_not_awaited()
                    message = str(caught.exception)
                    self.assertIn("target: engine executable", message)
                    self.assertIn(executable, message)
                    self.assertIn("was not found", message)
                    self.assertIn("engine command=", message)
                    self.assertIn(str(Path.cwd()), message)

    async def test_unknown_engine_keeps_initial_error_before_long_usage_text(self):
        player = self.engine("unknown-engine")
        with self.assertRaises(autoplay.ProtocolError) as caught:
            await player.start()
        message = str(caught.exception)
        self.assertIn("failed the 5DUCI startup handshake", message)
        self.assertIn("expected '5duciok'", message)
        self.assertIn("Unknown engine: ghost", message)
        self.assertIn("usage line 49", message)
        self.assertIn("exited with 2", message)
        self.assertIn(repr(player.command), message)

    async def test_non_engine_program_exiting_successfully_is_rejected(self):
        player = self.engine("not-an-engine")
        with self.assertRaises(autoplay.ProtocolError) as caught:
            await player.start()
        self.assertIn("expected '5duciok'", str(caught.exception))
        self.assertIn("exited with 0", str(caught.exception))
        self.assertIn("Usage: this program", str(caught.exception))

    async def test_timeout_names_the_missing_handshake_response(self):
        for mode, request, response in (("no-5duciok", "5duci", "5duciok"),
                                        ("no-readyok", "isready", "readyok")):
            with self.subTest(mode=mode):
                player = self.engine(mode)
                with self.assertRaises(autoplay.ProtocolError) as caught:
                    await player.start()
                message = str(caught.exception)
                self.assertIn(f"sent {request!r}, expected {response!r}", message)
                self.assertIn("timed out", message)
                self.assertIn("Check the engine name/arguments", message)
                await player.close()

    async def test_readiness_exit_reports_engine_error_and_failed_barrier(self):
        player = self.engine("ready-exit")
        with self.assertRaises(autoplay.ProtocolError) as caught:
            await player.start()
        message = str(caught.exception)
        self.assertIn("sent 'isready', expected 'readyok'", message)
        self.assertIn("Cannot initialize engine backend", message)
        self.assertIn("exited with 3", message)

    async def test_success_requires_both_handshake_responses(self):
        player = self.engine("legal")
        await player.start()
        self.assertEqual(player.phase, "idle")
        self.assertEqual(list(player.protocol_log),
                         ["> 5duci", "< 5duciok", "> isready", "< readyok"])


if __name__ == "__main__":
    unittest.main()

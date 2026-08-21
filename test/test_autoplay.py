from __future__ import annotations

import re
import unittest
from types import SimpleNamespace

import autoplay


class FakeGame:
    def __init__(self) -> None:
        self.metadata = {"size": "8x8", "result": "stale"}
        self.comments = ["Existing comment."]

    def get_comments(self):
        return list(self.comments)

    def set_comments(self, comments) -> None:
        self.comments = list(comments)


class AutoplayMetadataTest(unittest.TestCase):
    def setUp(self) -> None:
        self.arguments = SimpleNamespace(
            white="white --engine",
            black="black --engine",
            white_name="White Engine",
            black_name="",
            event="Experiment A",
            site="Local",
        )

    def test_standard_headers_replace_stale_input_metadata(self) -> None:
        game = FakeGame()
        autoplay.configure_pgn_metadata(game, self.arguments, round_number=2)
        self.assertEqual(game.metadata["event"], "Experiment A")
        self.assertEqual(game.metadata["site"], "Local")
        self.assertRegex(game.metadata["date"], re.compile(r"^\d{4}\.\d{2}\.\d{2}$"))
        self.assertEqual(game.metadata["round"], "2")
        self.assertEqual(game.metadata["white"], "White Engine")
        self.assertEqual(game.metadata["black"], "black --engine")
        self.assertEqual(game.metadata["result"], "*")
        self.assertEqual(game.metadata["size"], "8x8")

    def test_elo_headers_override_site_and_add_matchid(self) -> None:
        game = FakeGame()
        autoplay.configure_pgn_metadata(
            game,
            self.arguments,
            round_number=3,
            site="Local/Batch 9",
            match_id=57,
        )
        self.assertEqual(game.metadata["site"], "Local/Batch 9")
        self.assertEqual(game.metadata["round"], "3")
        self.assertEqual(game.metadata["matchid"], "57")

    def test_outcomes_use_standard_result_tokens(self) -> None:
        expected = {
            "white": "1-0",
            "black": "0-1",
            "draw": "1/2-1/2",
            "cap": "1/2-1/2",
            "protocol": "*",
            "error": "*",
        }
        for outcome, result in expected.items():
            with self.subTest(outcome=outcome):
                game = FakeGame()
                autoplay.set_pgn_result(game, outcome)
                self.assertEqual(game.metadata["result"], result)

    def test_termination_comment_is_appended_and_sanitized(self) -> None:
        game = FakeGame()
        autoplay.append_termination_comment(game, " Interrupted {by user}.\n")
        self.assertEqual(
            game.comments,
            ["Existing comment.", "Interrupted (by user)."],
        )

    def test_protocol_comment_identifies_engine_and_failure(self) -> None:
        player = autoplay.EngineProcess("flat-uct", "engine flat-uct", 100)
        comment = autoplay.protocol_termination_comment(
            player,
            1,
            autoplay.ProtocolError("black: exited with 9"),
        )
        self.assertEqual(comment, "Black [flat-uct] crashed.")


if __name__ == "__main__":
    unittest.main()

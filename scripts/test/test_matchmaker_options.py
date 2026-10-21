"""Shared CLI resource limits and prevention of repeated explicit RNG streams."""

import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import elo_matchmaker
import matchmaker_options as common
import ratio_matchmaker


class ExecutionOptionsTests(unittest.TestCase):
    def elo_args(self, *options):
        parser = elo_matchmaker.build_parser()
        return parser, parser.parse_args(["run", "--games", "4", *options])

    def test_jobs_alias_and_default_agree_in_both_matchmakers(self):
        for parser, prefix in ((elo_matchmaker.build_parser(), ["run", "--games", "1"]),
                               (ratio_matchmaker.build_parser(), ["--target", "unused"])):
            self.assertEqual(parser.parse_args(prefix).jobs, 1)
            self.assertEqual(parser.parse_args([*prefix, "-j", "3"]).jobs, 3)

    def test_partial_named_pool_keeps_each_group(self):
        parser, args = self.elo_args("-j", "2", "--core-group", "P=0,1", "--core-group", "E=2,3")
        with patch.object(common.cpu_affinity, "available_cpus", return_value={0, 1, 2, 3}):
            common.configure_cpus(parser, args)
        self.assertEqual(args.core_slots, [(0, "P"), (2, "E")])
        self.assertEqual(args.controller_cpus, [1, 3])

    def test_group_coverage_and_oversubscription_fail_explicitly(self):
        for options in (("-j", "1", "--core-group", "P=0,1", "--core-group", "E=2,3"),
                        ("-j", "5")):
            parser, args = self.elo_args(*options)
            with patch.object(common.cpu_affinity, "available_cpus", return_value={0, 1, 2, 3}), \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                common.configure_cpus(parser, args)

    def test_unsupported_affinity_falls_back_only_for_automatic_selection(self):
        parser, args = self.elo_args("-j", "2")
        with patch.object(common.cpu_affinity, "available_cpus", return_value=None), \
                contextlib.redirect_stderr(io.StringIO()) as warning:
            common.configure_cpus(parser, args)
        self.assertEqual(args.worker_cores, [None, None])
        self.assertIn("unpinned", warning.getvalue())
        parser, args = self.elo_args("--core", "0")
        with patch.object(common.cpu_affinity, "available_cpus", return_value=None), \
                contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            common.configure_cpus(parser, args)

    def test_no_pin_does_not_query_affinity_or_limit_jobs_to_cpu_count(self):
        parser, args = self.elo_args("--no-pin", "-j", "8")
        with patch.object(common.cpu_affinity, "available_cpus", side_effect=AssertionError):
            common.configure_cpus(parser, args)
        self.assertEqual(args.worker_cores, [None] * 8)


class EngineRandomnessTests(unittest.TestCase):
    def test_default_commands_do_not_receive_injected_seeds(self):
        command = "./build/5dchess zero"
        self.assertEqual(common.EngineSeeds().prepare(command), (command, None))

    def test_fixed_seeds_are_rejected_in_matchmaker_commands(self):
        for command in ("engine --seed 42", "engine --seed=42", "./build/5dchess zero -s 42"):
            with self.subTest(command=command), self.assertRaisesRegex(ValueError, "fixed engine seeds"):
                common.validate_engine_command(command)

    def test_duplicate_entropy_draws_do_not_reuse_a_process_seed(self):
        seeds = common.EngineSeeds()
        with patch.object(common.secrets, "randbits", side_effect=[7, 7, 8]):
            first = seeds.prepare("engine --seed {seed}")
            second = seeds.prepare("engine --seed {seed}")
        self.assertEqual(first, ("engine --seed 7", 7))
        self.assertEqual(second, ("engine --seed 8", 8))


if __name__ == "__main__":
    unittest.main()

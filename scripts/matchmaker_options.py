"""Shared resource, output, and engine-randomness policies for matchmakers."""

from dataclasses import dataclass, field
from datetime import datetime
import json
from pathlib import Path
import secrets
import shlex
import sys

import cpu_affinity


def add_execution_options(parser):
    parser.add_argument("--jobs", "-j", type=int, default=1,
                        help="maximum concurrent games")
    parser.add_argument("--core", dest="cores", type=int, action="append", default=[],
                        help="engine logical CPU; repeat for a pool (default: automatic)")
    parser.add_argument("--controller-core", dest="controller_cores", type=int,
                        action="append", default=[],
                        help="controller/arbiter logical CPU; repeat for a pool (default: remaining CPUs)")
    parser.add_argument("--no-pin", action="store_true", help="disable CPU affinity explicitly")


def configure_cpus(parser, args):
    """Choose distinct engine slots, retaining every explicitly named group."""
    groups = getattr(args, "core_groups", None) or []
    if args.cores and groups:
        parser.error("--core and --core-group cannot be combined")
    slots = [(cpu, None) for cpu in args.cores]
    names = set()
    for specification in groups:
        try:
            name, values = specification.split("=", 1)
            name = name.strip()
            cpus = [int(value) for value in values.split(",")]
        except ValueError:
            parser.error("--core-group must use NAME=CPU,CPU,...")
        if not name or name in names or not cpus:
            parser.error("--core-group names must be non-empty and unique")
        names.add(name)
        slots.extend((cpu, name) for cpu in cpus)
    args.controller_cpus = []
    args.ignore_execution_groups = False
    if args.no_pin:
        if slots or args.controller_cores:
            parser.error("--no-pin cannot be combined with CPU pool options")
        args.core_slots = [(None, None)] * args.jobs
        args.worker_cores = [None] * args.jobs
        return
    try:
        available = cpu_affinity.available_cpus()
    except OSError as exc:
        parser.error(f"cannot query CPU affinity: {exc}")
    if available is None:
        if slots or args.controller_cores:
            parser.error("explicit CPU selection requires OS CPU-affinity support")
        print("Warning: CPU affinity unavailable; using unpinned jobs.", file=sys.stderr)
        args.core_slots = [(None, None)] * args.jobs
        args.worker_cores = [None] * args.jobs
        return
    selected = [cpu for cpu, _ in slots]
    for option, cpus in (("engine pool", selected), ("controller pool", args.controller_cores)):
        if len(cpus) != len(set(cpus)) or any(cpu not in available for cpu in cpus):
            parser.error(f"{option} CPUs must be unique and available; available CPUs: "
                         + ",".join(map(str, sorted(available))))
    if set(selected) & set(args.controller_cores):
        parser.error("engine and controller CPU pools must not overlap")
    if not slots:
        slots = [(cpu, None) for cpu in sorted(available - set(args.controller_cores))]
    if args.jobs > len(slots):
        parser.error("--jobs cannot exceed the available engine CPUs; select more CPUs "
                     "or explicitly use --no-pin")
    if args.jobs < len(names):
        parser.error("--jobs must provide at least one slot per --core-group")
    if groups:
        # Round-robin the groups before truncating the pool to jobs. Taking a
        # raw prefix can silently drop a required hardware class on resume.
        pools = [[slot for slot in slots if slot[1] == name] for name in dict.fromkeys(name for _, name in slots)]
        slots = []
        while any(pools):
            for pool in pools:
                if pool:
                    slots.append(pool.pop(0))
    args.core_slots = slots[:args.jobs]
    args.worker_cores = [cpu for cpu, _ in args.core_slots]
    args.controller_cpus = args.controller_cores or sorted(available - set(args.worker_cores))


def add_output_options(parser, default_parent):
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--logs-dir", type=Path, default=Path(default_parent),
                       help="parent directory for automatically named run directories")
    group.add_argument("--output-dir", type=Path,
                       help="exact run directory (must be new, except when resuming)")


def create_run_directory(args, *, resume_name=None):
    path = args.output_dir or args.logs_dir / (resume_name or datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
    path.mkdir(parents=True, exist_ok=resume_name is not None)
    return path.resolve()


def validate_engine_command(command):
    """Keep repeated fixed RNG streams out of independent game samples."""
    tokens = shlex.split(command)
    native = tokens and Path(tokens[0]).name.lower() in ("5dchess", "5dchess.exe")
    for index, token in enumerate(tokens):
        value = None
        if token == "--seed" or (native and token == "-s"):
            value = tokens[index + 1] if index + 1 < len(tokens) else ""
        elif token.startswith("--seed="):
            value = token.split("=", 1)[1]
        if value is not None and value != "{seed}":
            raise ValueError("fixed engine seeds would repeat across games; omit the seed option "
                             "for fresh engine randomness, or use --seed {seed} for automatic seeds")


@dataclass
class EngineSeeds:
    """Allocate distinct 32-bit seeds within this run, only for explicit templates."""
    used: set[int] = field(default_factory=set)

    def prepare(self, command):
        validate_engine_command(command)
        if "{seed}" not in command:
            return command, None
        if len(self.used) >= 2**32:
            raise ValueError("engine seed space exhausted")
        seed = secrets.randbits(32)
        while seed in self.used:
            seed = secrets.randbits(32)
        self.used.add(seed)
        return command.replace("{seed}", str(seed)), seed


def record_engine_launch(directory, *, role, command, seed, cpu, **context):
    row = dict(role=role, command=command, seed=seed, cpu=cpu, **context)
    with (directory / "engine-launches.jsonl").open("a") as output:
        output.write(json.dumps(row) + "\n")

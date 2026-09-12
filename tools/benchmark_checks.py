#!/usr/bin/env python3
"""Compare two Release builds of test/55332 (each must enumerate 55,332 actions)."""

import argparse
import json
import platform
import statistics
import subprocess
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--runs", type=int, default=6)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    binaries = {
        "baseline": str(args.baseline.resolve()),
        "candidate": str(args.candidate.resolve()),
    }
    samples = {name: [] for name in binaries}
    warmup = {}
    for round_index in range(args.runs + 1):
        names = list(binaries)
        if round_index % 2:
            names.reverse()
        for name in names:
            start = time.perf_counter()
            subprocess.run([binaries[name]], check=True)
            elapsed = time.perf_counter() - start
            if round_index == 0:
                warmup[name] = elapsed
            else:
                samples[name].append(elapsed)
            print(f"{name} round {round_index}: {elapsed:.6f} s", flush=True)
    medians = {name: statistics.median(values) for name, values in samples.items()}
    result = {
        "platform": platform.platform(),
        "binaries": binaries,
        "warmup_seconds": warmup,
        "seconds": samples,
        "median_seconds": medians,
        "speedup": medians["baseline"] / medians["candidate"],
    }
    report = json.dumps(result, indent=2) + "\n"
    print(report, end="")
    if args.output:
        args.output.write_text(report)


if __name__ == "__main__":
    main()

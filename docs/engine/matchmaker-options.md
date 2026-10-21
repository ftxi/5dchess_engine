# Common matchmaker options

`elo_matchmaker.py` and `ratio_matchmaker.py` share execution and logging
conventions. Elo's options follow `run` or `resume`; ratio's follow the script
name directly. Their rating and statistical options remain specific to each
experiment.

## Jobs and CPU placement

`--jobs N` / `-j N` limits concurrent games and defaults to 1 in both tools.
On Linux and Windows, automatic placement assigns each job a distinct allowed
logical CPU. Both engines in a game use that CPU; ratio's baseline meter also
uses it. The controller, game validation, and adjudication subprocesses use
remaining allowed CPUs by default. If none remain, controller work shares OS
scheduling with engines.

- Repeat `--core CPU` to select an engine pool instead of automatic selection.
- Repeat `--controller-core CPU` to select a disjoint controller/arbiter pool.
- `--no-pin` disables pinning and permits more jobs than available CPUs.
- If affinity is unsupported, automatic selection warns and runs unpinned.
  Explicit CPU options fail rather than silently ignore the requested placement.
- If affinity is supported, jobs exceeding the selected CPU count fail.
  Affinity errors are reported rather than silently disabling pinning.

Logical CPU IDs do not identify physical cores or P/E classes uniformly across
machines. Automatic selection does not detect those distinctions. Select the
pool explicitly when they matter; SMT siblings still share physical resources.
Pinning does not reserve CPUs against unrelated applications or OS activity.
Elo additionally supports named `--core-group` pools; see its
[matchmaker guide](elo-matchmaker.md).

## Output directories and openings

`--logs-dir PATH` selects a parent for automatically named run directories.
Defaults are `logs/elo-runs` and `logs/ratio-runs`. Alternatively, `--output-dir
PATH` selects the exact run directory. These flags are mutually exclusive.
New runs require a new directory; Elo `resume` can reuse one.

`--opening FILE` / `-g FILE` reads a starting PGN. `--game` is an alias.
`--opening-text PGN` / `-m PGN` supplies inline PGN instead of a file. Ratio can
repeat `--opening` to cycle files by pair number; Elo accepts one file per
invocation. `--event NAME` sets the PGN event.

`--module-dir`, `--max-actions`, and `--timeout` have the same meanings and
defaults. All search and timeout durations are in milliseconds. Elo's
`--movetime` applies to either player; ratio's `--baseline-movetime` applies to
the baseline and meter, with a separate target iteration budget and time cap.

## Engine randomness and matchmaking seeds

No engine seed is required. The default baseline command and recommended
engine commands omit a seed, using fresh randomness supplied by the engine.
The native `5dchess` engines use `std::random_device` when no seed is supplied.

To explicitly record seeds, use an optional `{seed}` command template, such as
`"./build/5dchess zero --seed {seed}"`. A matchmaker assigns each engine
process a fresh 32-bit seed, rejecting duplicate draws within that invocation.
The target, baseline opponent, meter, calibration processes, and both games of
a color-reversed pair receive separate assignments. Ratio changes retain the
same allocator, so a new ratio does not restart a seed sequence.

Recognized fixed seed options (`--seed 123`, `--seed=123`, and native
`5dchess -s 123`) are rejected by the matchmakers to prevent silently repeating
the same RNG stream across games. Standalone engine and autoplay commands
continue to accept fixed seeds for debugging. The old ratio `--seed-start`
option is removed.

`engine-launches.jsonl` records the expanded command, assigned CPU, game/match
context, and explicit seed for every launch. A null seed means the engine
manages its own randomness; the matchmaker does not claim to know that seed.
Assigned seeds are distinct within an invocation, not a global uniqueness
guarantee across unrelated runs. Separate seeds alone do not prove independent
outcomes, and time-limited searches are not exactly reproducible from a seed.
Ratio still treats a whole color-reversed pair as one statistical sample.

Elo's `--matchmaking-seed` controls its opponent-selection RNG, not engine
randomness. Its default is 0. Reusing it may reproduce pairing choices; it does
not cause engine processes to share seeds. The Elo matchmaker's old `--seed`
flag is removed.

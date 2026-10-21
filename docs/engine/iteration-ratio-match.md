# Iteration-ratio matches

`scripts/ratio_matchmaker.py` compares a **target** with a **baseline** at
position-scaled iteration budgets. Build the native engine and Python binding
first, using the [build guide](../usage/building.md#python-module).

```sh
python scripts/ratio_matchmaker.py \
  --target "./build/5dchess zero-capture-check-pw" \
  --baseline "./build/5dchess zero" \
  --jobs 12 --ratios 1,.75,.5,.25 --baseline-movetime 400
```

The executable's `zero` policy is the default baseline, but either command
can select another engine. The obsolete `--zero`, `--zero-movetime`,
`--workers`, and `--seed-start` options have been removed. Concurrency is
`--jobs` / `-j`, with a default of 1.

Both matchmakers share [CPU, output, opening, and randomness options](matchmaker-options.md).
Engine seeds are not required. Defaults use each engine's fresh randomness;
optional `{seed}` templates receive distinct seeds for every process, including
calibration and the meter, rather than a seed shared across a pair or ratio.

## Budgets and samples

On a baseline turn, the baseline searches for `--baseline-movetime`
milliseconds. On a target turn, a separate baseline meter first searches the
same position for that duration. If it completes `N` iterations, the target
receives `m * N` iterations, rounded up to a multiple of ten. `m` is the
iteration ratio, not a wall-time ratio.

The target also has a safety time cap based on measured target/baseline
throughput on that worker's CPU. `--cap-factor` defaults to 2; the absolute
limit is `--max-target-movetime`. Each selected CPU is calibrated before games
start. Target searches stopped short of their iteration limit have
`time_capped=1` in `searches.csv`; their playing results concern the capped
configuration.

Each sample is two games sharing an opening with target colors reversed.
They no longer deliberately share engine seeds; the two games still constitute
one statistical sample. A game scores 1 for a target win, 0.5 for a draw, and
0 for a loss.
The pair score is the average of those two scores. Void games exclude their
entire pair from evidence. Action-limit games count as draws; this is an
experimental cap, not verified board adjudication.

## CPU assignment and continuous scheduling

`--jobs` limits concurrent games. With affinity enabled, each worker runs both
color-reversed games of a pair sequentially on the same logical CPU. The target, baseline opponent,
and baseline meter share that CPU. The worker immediately starts another pair
when free; there is no batch barrier. The controller and adjudication use the
remaining CPU pool by default.

CPU availability checks, unsupported-platform fallback, `--no-pin`, explicit
pools, SMT limitations, and shared output options are described in
[Common matchmaker options](matchmaker-options.md).

For example, on a machine where these logical CPU IDs are available:

```sh
python scripts/ratio_matchmaker.py \
  --target "./build/5dchess zero-capture" \
  -j 2 --core 2 --core 4 --controller-core 0 --controller-core 1
```

## Adaptive ratio selection

With the default `--stat-method anytime`, scheduling defaults to
`--schedule adaptive`:

1. Start at the highest listed ratio. After at least 8 valid pairs, descend
   provisionally when current acceptable evidence reaches E=3. Change these
   heuristics with `--switch-min-pairs` and `--switch-e`.
2. If the lower ratio's current unacceptable evidence reaches E=3, refine the
   two neighboring ratios. Allocate three of every four new pairs to the
   higher ratio until it is certified, with the fourth testing the lower one.
   A ratio that is certified or formally rejected receives no new pairs.
3. If the lower neighbor is certified, explore the next lower ratio. If the
   higher neighbor is formally rejected, move the boundary upward. A weak
   reversal signal does not immediately trigger another descent.
4. Stop when the neighboring ratios are resolved, the lowest listed ratio is
   certified, the highest ratio is formally rejected, or the relevant ratios
   exhaust their pair budgets. Unresolved ratios stay inconclusive.

Each ratio keeps its own evidence, sample order, and `--max-pairs` budget
(default 256). Revisits retain all evidence. Passing a low ratio never
manufactures a result at another ratio or rules out every still-lower ratio.
In-flight pairs finish during provisional switches and may update their
original ratio. The final stop cancels the outstanding work. Ctrl-C also
closes engine processes and prints a summary.

Results enter evidence in pair-number order within each ratio, independently
of completion time. Later completed pairs wait in a buffer while their CPUs
continue working. Only a contiguous prefix of complete pairs is used for
inference; cancelled pairs and any completed pairs beyond a final gap are
excluded. Game logs can therefore contain more finished games than the
statistical sample includes.

`--schedule sequential` waits for formal evidence before assigning new pairs
to the next ratio, while still refilling workers continuously. With
`--advance-on better`, descent requires superiority instead of
noninferiority.

## Evidence and live output

A header before calibration explains every live-output field:

| Field | Meaning |
| --- | --- |
| `m` | Target/baseline-meter iteration budget ratio |
| `games N; target W-D-L` | Finished game count and target wins, draws, losses |
| `pairs W-T-L` | Committed pairs with score above, equal to, or below 0.5 |
| `mean score` | Average normalized score over valid committed pairs |
| `target/baseline IPS` | Aggregate iterations-per-second ratio, including meter searches |
| `current E superior/acceptable/unacceptable` | Current evidence for each strength claim |
| `anytime p` | `min(1, 1 / historical maximum E)` for the corresponding claim |

The default noninferiority margin is 0.10. Acceptable evidence tests mean pair
score above 0.40; unacceptable evidence tests below 0.40. Superiority tests
above 0.50. Thus a score of 0.45 can be acceptable without implying superiority.
E starts at 1. Larger values support the named claim; E is not a probability.

Provisional E=3 signals only allocate compute. Formal claims use a separate
run-wide `--alpha` budget, divided equally among every listed ratio and its
three claims. With four ratios and alpha=0.05, each formal threshold is
historical maximum E >= 240. Certification remains recorded when current
evidence declines. Incompatible formal acceptable/unacceptable crossings are
reported as `conflicting` and stop the search.

The bounded-score e-process assumes the relevant conditional mean null over
the sample stream. Ordered commitment and explicit error allocation address
scheduling and multiple claims; they do not establish that a chosen opening
suite represents every kind of game. Near-boundary results can remain
inconclusive at the pair limit.

`--stat-method fixed-looks` selects sequential scheduling and the older tests
at `--look-pairs` checkpoints. This legacy mode retains its original
inferiority definition (below 0.50), and spends each claim's allocated alpha
across the declared looks. It cannot be combined with adaptive scheduling.

The summary reports each ratio's status, the lowest directly certified ratio,
and the next lower ratio's status. `games.csv`, per-game PGNs, and
`searches.csv` are saved in the run directory; both CSVs record the assigned
logical CPU. `engine-launches.jsonl` records each command and any assigned seed.
The old shared pair `seed` column is no longer present in `games.csv`.
Use `--output-dir` for an exact new directory or `--logs-dir` for its parent
(default `logs/ratio-runs`). `--event` sets the PGN event name. `--opening-text`
accepts inline PGN instead of opening files.

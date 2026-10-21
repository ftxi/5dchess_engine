# Canonical engine experiments

Use the versioned [v1 position suite](../../test/engine_suite/v1/README.md)
and `scripts/canonical_test.py`. The reference is
`zero-capture-check-pw --pw-constant 2 --pw-alpha 0.5`.
Change one hypothesis at a time: for example victim values, attacker
preference, or branching cost. Small runs support intermediate engineering
decisions; an inconclusive statistical result does not reject a design.

## Build and freeze the reference

Run from the repository root, using the same Python for compilation and runs:

```sh
cmake --preset engine-testing -DPython_EXECUTABLE="$(command -v python3)"
cmake --build --preset engine-testing
ctest --preset engine-testing
python3 -m unittest discover -s scripts/test -p test_canonical_workflow.py -v
python3 scripts/canonical_test.py verify --native
python3 scripts/canonical_test.py freeze
```

The preset enables Release, LTO when supported, native tools, tests, and the
Python rules binding. `-fno-rtti` applies to the native libraries and
executables. The binding translation unit alone enables RTTI, as required
by pybind11. The wrapper checks the Release and `-fno-rtti` cache settings.

Freeze once **before changing engine code**. This copies the reference
executable to `logs/canonical/reference/5dchess`, with its SHA-256, exact Git
revision, working-tree status, compiler, and CMake settings. It refuses to
overwrite an existing reference. Rebuilding the candidate then leaves the
reference unchanged. A new reference should get a new `--reference` directory;
keep old references for comparison. Freezing another revision defines a new
experiment series; it does not recreate the original reference.

To measure a feature's incremental effect against another engine, freeze that
engine before changing its source, using a separate reference directory:

```sh
python3 scripts/canonical_test.py freeze \
  --engine 'mvvlva --pw-constant 2 --pw-alpha 0.5' \
  --reference logs/canonical/reference-mvvlva
```

Pass the same `--reference` directory to `run`. The selected reference command
is recorded in its manifest; the default remains the fixed zccpw reference.
This starts a separate experiment series. Do not combine its statistical
evidence with comparisons against zccpw.

The first local reference was built at `b6e16ec` (`v0.5.2-2-gb6e16ec`),
not v0.5.1. The selection manifest records that revision and executable hash.
Executables and run outputs are local, ignored artifacts; back them up if
exact binary reproducibility matters.

## Size and budgets

| Subset | Positions | Maximum games per ratio | Purpose |
|---|---:|---:|---|
| `quick` | 4 | 8 | Cheap initial screen |
| `standard` | 8 | 16 | Default experiment |
| `confirm` | 16 | 32 | Broaden a promising result |
| `pool` | 64 | 128 | Available collection; not a routine tournament |
| `stress` | 4 | 8 | Separate difficult cases |

The first three are nested prefixes. Every subset is balanced by actual side
to move. Each position is played twice with the engine assignments reversed
and the same seed. These are two different forms of balance: the first
controls the type of position sampled; the second controls which engine
receives each side. No colors or historical boards are artificially flipped.

The default run uses one ratio (`1`), one worker, 200 ms for each reference
search, a 2 s target safety cap, and a 160-action game cap. Target turns also
need a reference metering search, so game runtime includes that extra work.
These are explicit starting budgets, not guarantees of the final game length
or engine response time. The controller has a further 2 s response watchdog.

## Run a candidate

Build the candidate in `build-canonical`, then run:

```sh
python3 scripts/canonical_test.py run --subset quick \
  --engine 'zero-capture-check-pw --pw-constant 2 --pw-alpha 0.5' \
  --output logs/canonical/capture-quality-quick
```

Omit `--subset quick` for the standard eight positions. A candidate in another
Release build directory can use `--build`; that directory must also contain
the Python binding. Keep settings fixed when comparing related candidates.
`--ratios .5` tests a single proposed iteration-efficiency target; a descending
list such as `--ratios 1,.5` uses the matcher's normal advancement rules.

To extend an eight-position experiment without replaying those positions:

```sh
python3 scripts/canonical_test.py run --subset confirm --skip-first 8 \
  --output logs/canonical/capture-quality-additional-eight
```

Use exactly the same candidate, reference, ratios, and budgets. Seeds remain
tied to position order. Retain both run directories. Each controller run has
its own statistical evidence; do not add p-values or treat an extension as a
fresh independent confirmation. The raw paired outcomes can be inspected
together for intermediate decisions.

For a workflow smoke check only, use `--subset quick --movetime 20
--target-cap 300 --max-actions 2`. Its forced short games measure neither
strength nor equality of engines.

## Evidence and decisions

Every run saves candidate executable and Python rules-binding snapshots, the exact opening files,
`run.json`, `controller.log`, `games.csv`, `searches.csv`, per-game PGNs, and
`summary.json`. Existing output directories cannot be reused. The run manifest
records the suite hash, position IDs, commands, seeds, build settings, binding
hash, and candidate/reference executable hashes. Suite and source content
hashes are verified before launching games.

The summary separates outcomes by cost group and side to move. It also
counts protocol/legality failures, target searches which did not receive
their full iteration budget, and action-limit draws. Those draws are an
experimental cap, not a verified draw on the board. Controller failures must
be fixed, and capped budgets must be considered before attributing results
to the intended ratio. The existing matcher can count engine failures as
losses; the summary makes those visible.

The wrapper returns a nonzero exit status for recorded controller, protocol,
or legality failures even when the underlying matcher recorded them as losses.

Use the existing matcher's always-valid stopping procedure, with the fixed
position order and at most one pair per selected position at each ratio.
The small subsets will often remain statistically inconclusive. Use their
outcomes and search diagnostics to choose the next experiment rather than
automatically buying more games. Claims concern this selected suite and
configuration, not the entire distribution of human games. Per-ratio evidence
does not provide a correction across arbitrary experiments or ratio sweeps.

Assess a design's required iteration ratio separately from its current
throughput. If it needs fraction `m` of reference iterations and currently
achieves throughput fraction `q`, the approximate speedup needed is `m/q`
when that exceeds one. Then optimize promising candidates and confirm at
equal wall time using the existing autoplay tools. Iteration counts alone
do not establish equal-time strength.

## Suite maintenance

Do not automatically resample after an engine change or reclassify costs on
each run. v1's measured groups are frozen labels. A changed fixture, source
game, side balance, or subset causes verification to fail. Create a new suite
version for intentional selection changes.

The selector can reproduce the procedure into a new directory:

```sh
python3 scripts/canonical_test.py select --suite test/engine_suite/v2 \
  --audit logs/canonical/selection-v2
```

It scans Turn Zero games, retains completed positions at least eight actions
from either end, samples width/history/advantage/side strata, and probes 256
distinct games. It uses two collection workers, bounded subprocesses, then
sequential probes. All source errors and raw candidate/probe records are
retained in the audit directory. Cost is the maximum of the first ten-
iteration search time and the observed time scaled to 100 iterations.
This includes root construction and is a short operational cost measurement,
not a universal cost model. Selection timings depend on the machine and load;
the committed manifest and snapshots, rather than rerunning selection, define
the canonical suite.

# HC depletion corpus

The fixed corpus contains 275 distinct positions described by original PGN files
and optional submitted-turn selectors. There are no copied intermediate games.
The original dedicated fixtures `30134.5dpgn`, `55332.5dpgn`, and
`four-timeline-knights.5dpgn` remain in this directory.

The default [profiling configuration](../../../scripts/profile_hc_games.json)
includes 20 count positions alongside the existing checkmate and perftest cases.
The [expanded configuration](../../../scripts/profile_hc_expanded.json) runs only
the other 255 count positions. From the repository root:

```sh
python scripts/profile_hc.py --workload count
python scripts/profile_hc.py --config scripts/profile_hc_expanded.json
```

Each count entry specifies `file`, `expected`, and optionally `at`, for example
`{"file": "test/pgn/small.5dpgn", "at": "1b", "expected": "=602"}`. The turn is
the last submitted action to include, as in `5dtools count --at`; `0` selects the
initial position and omission selects the final position. The runner sends
`EXPECTED=602 AT=1b COUNT <game>` to native profiling. No Python engine binding is
required. See the [profiling guide](../../../docs/usage/tools.md) for other options.

## Selection and provenance

The corpus samples quarter, half, three-quarter, penultimate, and final action
positions from all root PGNs, and half, three-quarter, and penultimate positions
from 68 online games. These include all 20 games outside Standard - Turn Zero and
48 Turn Zero games selected by sorting SHA-256 of `paired-expanded-v1:` followed
by the filename. The original four count controls are included. Repeated action
indices and identical historical states with the same board configuration were
deduplicated.

The default 20 retain the four controls and cover small and terminal spaces,
mandatory and optional timelines, all sampled variant groups, costly complete
enumerations, and large capped searches. Selection is based on this coverage
rather than an optimization's speedup ranking. The [manifest](manifest.json)
records source games and hashes, submitted action indices and turn selectors,
timeline counts, count expectations, and each default selection's purpose.
Action indices in provenance count completed actions from zero, not PGN turns.

Known spaces up to 200,000 actions use `=N`; larger spaces use `>50000` and stop at
50,001 actions. The latter do not measure complete depletion. Separate prefixes
of 10, 100, and 1,000 actions are enabled in both configurations. The manifest
distinguishes known complete counts from lower bounds; some counts above 200,000
were established during screening but remain capped in routine profiling.

Online sources retain the [online collection's attribution](../online/README.md)
and [t0database license](../online/LICENSE.t0database). Source aliases in the
manifest identify the original games and their metadata. Generated measurements
are separate from the corpus.

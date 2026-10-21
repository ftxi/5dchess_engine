# MVV-LVA candidate ordering

Measured on October 21, 2026 using the [canonical workflow](../engine/testing.md).

## Change

The `mvvlva` engine shares zccpw's zero default policy, check feature and +300
check bonus, progressive widening, backpropagation, and final move selection.
Only capture ordering changes. There are no rollouts, attack/defence queries,
or exchange evaluation.

Victim ranks follow the existing 5D capture-value groups: pawn 1, brawn 2,
rook/common king 3, bishop/knight 4, unicorn/dragon 5, princess 6, and queen 7.
Royal queens use victim rank 7; royal attackers rank 8. Capture scores are
`480 + 16 * (victim_rank - 1) + (8 - attacker_rank)`. This orders victims first,
then cheaper attackers, while retaining the previous capture/check priority
groups. Quiet moves receive no attacker penalty. Physical and arriving HC
coordinates receive the score; departures and null coordinates remain zero.

## Configuration and reproduction

The existing frozen reference is zccpw at `b6e16ec58efd6a0c865fa0afd808f170b48794d1`,
with executable SHA-256
`febf14e85aed8d2f5a20d6eea991fc1255714713dba401fe861fda482e149bd2`.
It was verified and retained without overwriting. The candidate is a local
working-tree change based on the same revision, with executable SHA-256
`20aed5def0f9a6c41b9377079254d339b041be1feaea93f4b8d962ff0076629a`.

Both use Release, GCC 15.2.0, LTO, and native `-fno-rtti`; the Python binding
uses its required RTTI exception. Widening constant is 2 and exponent is 0.5.

```sh
cmake --preset engine-testing -DPython_EXECUTABLE="$(command -v python3)"
cmake --build --preset engine-testing
ctest --preset engine-testing
python3 -m unittest discover -s scripts/test -p test_canonical_workflow.py -v
python3 scripts/canonical_test.py verify --native
python3 scripts/canonical_test.py run \
  --engine 'mvvlva --pw-constant 2 --pw-alpha 0.5' \
  --output logs/canonical/mvvlva-standard-20261021
```

The standard subset uses p01–p08, each with reversed engine assignments and
a shared seed. The iteration ratio is 1, with 200 ms reference searches,
adaptive target safety caps bounded by 2000 ms, one worker, and a 160-action
game cap. These are iteration-budget comparisons, not equal-wall-time matches.
To repeat the experiment, use a fresh output directory and retain the same
frozen reference.

## Results

All 16 games completed. Results below are from the candidate's perspective.

| Position cost group | Wins | Draws | Losses |
| --- | ---: | ---: | ---: |
| Cheap | 4 | 0 | 2 |
| Moderate | 4 | 0 | 2 |
| Expensive | 1 | 2 | 1 |
| Total | 9 | 2 | 5 |

The candidate scored 62.5%, with two won pairs, six tied pairs, and no lost
pairs. The canonical always-valid procedure returned **inconclusive**:
superiority p = 0.6944 and noninferiority p = 0.501. This small experiment
does not establish a strength improvement or noninferiority.

Aggregate candidate/reference throughput ratio was 1.007 (113,703.5 versus
112,885.5 iterations/second); the matched-position geometric ratio was 0.992.
These measurements include the search behavior induced by each ordering.

There were no controller, protocol, or legality failures, no void games, and
no action-limit draws. One target search in pair 4, game 8, action 79 hit its
adaptive 401 ms cap: it completed 19,928 of the 26,710 iteration limit
(26,706 before rounding). That game was a candidate win. This budget shortfall
limits attribution of every game to the nominal iteration ratio.

Local evidence is saved under `logs/canonical/mvvlva-standard-20261021/`,
including `run.json`, `summary.json`, `controller.log`, search/game CSVs,
PGNs, and candidate/binding snapshots. Both controller and wrapper exited 0.

## Validation

All 40 native CTest tests and all four canonical-workflow Python tests passed,
with no skips. Native verification accepted all 64 frozen suite positions.
Regression coverage checks victim priority, cheaper-attacker preference,
variant groups, colors and unmoved flags, unchanged check bonuses, quiet
moves, timeline arrivals/departures, promotions, en passant, and cancellation.
CLI checks accepted seed/widening options and rejected invalid widening
parameters and rollout-only options for `mvvlva`.

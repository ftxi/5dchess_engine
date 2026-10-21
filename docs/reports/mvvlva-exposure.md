# Local capture exposure experiment

Measured on October 21, 2026 using the [canonical workflow](../engine/testing.md).

This report describes the original exposure-only implementation. The current
engine also includes [promotion gain and defence-aware exposure](mvvlva-exposure-v2.md).

## Change

`mvvlva-exposure` is a separate engine with the same zero default policy,
check detection and +300 bonus, progressive widening, backpropagation, and
final move selection as `mvvlva`. The original `mvvlva` remains available.

For captures only, it inspects the cached destination board after the move.
If an enemy piece physically attacks the arrival square, it subtracts
`min(128, max(0, arriving_value - victim_value) / 10)` from the MVV-LVA score.
The arriving piece's value includes promotion. Material values are shared
with the existing capture-reward constants, whose numeric values are unchanged.
Royal kings have no finite material value; royal safety remains a legality
concern. Equal/favorable material exchanges and quiet moves have no penalty.

The scale of 10 material units per ordering point and cap of 128 were fixed
before either experiment; there was no tuning against these results. The cap
retains the existing capture/check priority groups: captures score at least
352, above quiet checks (300), and checking captures score at least 652,
above non-checking captures (at most 583).

Attacks are geometric, so potentially pinned attackers count. Absence of a
physical attack does not establish safety across timelines. The feature uses
the arrival board for both physical and superphysical captures; departures
and null coordinates score zero. It does not credit friendly defence or
evaluate departure threats, cross-timeline attacks, or later exchanges.

## Reproduction and provenance

Before changing engine source, MVV-LVA was frozen using:

```sh
python3 scripts/canonical_test.py freeze \
  --engine 'mvvlva --pw-constant 2 --pw-alpha 0.5' \
  --reference logs/canonical/reference-mvvlva
```

The workflow now accepts `freeze --engine` to preserve an explicit reference
command. Its default remains zccpw. The MVV-LVA reference executable has
SHA-256 `20aed5def0f9a6c41b9377079254d339b041be1feaea93f4b8d962ff0076629a`,
identical to the prior [MVV-LVA experiment](mvvlva.md). The original zccpw
reference was retained without overwriting. Both references are based on
`b6e16ec58efd6a0c865fa0afd808f170b48794d1`; MVV-LVA includes its local
working-tree implementation.

The exposure candidate executable has SHA-256
`8e7cd986723ef96155a0b410ac74d1887ec80b1092f87ebba6a670a95d61d50d`.
Builds use Release, GCC 15.2.0, LTO, and native `-fno-rtti`, with the Python
binding's required RTTI exception.

After building with the `engine-testing` preset:

```sh
ctest --preset engine-testing
python3 -m unittest discover -s scripts/test -p test_canonical_workflow.py -v
python3 scripts/canonical_test.py verify --native
python3 scripts/canonical_test.py run \
  --engine 'mvvlva-exposure --pw-constant 2 --pw-alpha 0.5' \
  --reference logs/canonical/reference-mvvlva \
  --output logs/canonical/mvvlva-exposure-vs-mvvlva-standard-20261021
python3 scripts/canonical_test.py run \
  --engine 'mvvlva-exposure --pw-constant 2 --pw-alpha 0.5' \
  --output logs/canonical/mvvlva-exposure-vs-zccpw-standard-20261021
```

Each run uses the frozen standard p01–p08 subset, reversed engine assignments
and shared seeds, iteration ratio 1, 200 ms reference searches, adaptive target
safety caps bounded by 2000 ms, a 160-action game limit, and one worker. Runs
are sequential to avoid competing for CPU. Both use the same candidate binary.
Repeat runs require fresh output directories. These are separate experiment
series with separate evidence; their p-values must not be combined. Iteration
budgets do not establish equal-wall-time strength.

## Direct comparison against MVV-LVA

All 16 games completed: **5 wins, 3 draws, 8 losses**, a 40.625% candidate score.
The eight position pairs contained zero won pairs, six ties, and two losses.
The canonical procedure returned **inconclusive**: superiority anytime
p = 1, noninferiority p = 0.906, inferiority p = 0.7576.

| Position cost group | Wins | Draws | Losses |
| --- | ---: | ---: | ---: |
| Cheap | 3 | 0 | 3 |
| Moderate | 1 | 1 | 4 |
| Expensive | 1 | 2 | 1 |

Aggregate candidate/reference throughput ratio was 1.005 (110,175.3 versus
109,664.4 iterations/second); the matched-position geometric ratio was 0.990.
These measurements include the search behavior induced by each ordering.

There were no controller, protocol, or legality failures, void games, or
action-limit draws. One target search in pair 4, game 8, action 47 hit its
adaptive 401 ms cap: it completed 11,605 of 28,580 iterations (28,573 before
rounding). That game was a candidate win. This shortfall qualifies claims
about identical iteration budgets across every game.

## Comparison against the original zccpw reference

All 16 games completed: **8 wins, 3 draws, 5 losses**, a 59.375% candidate score.
The pairs contained two wins, five ties, and one loss. The canonical procedure
returned **inconclusive**: superiority anytime p = 0.7716, noninferiority
p = 0.5566, inferiority p = 0.9091.

| Position cost group | Wins | Draws | Losses |
| --- | ---: | ---: | ---: |
| Cheap | 3 | 0 | 3 |
| Moderate | 3 | 1 | 2 |
| Expensive | 2 | 2 | 0 |

Aggregate candidate/reference throughput ratio was 0.999 (110,630.2 versus
110,689.3 iterations/second); the matched-position geometric ratio was 1.002.
There were no controller, protocol, or legality failures, void games, or
action-limit draws. One target search in pair 5, game 10, action 4 hit its
adaptive 399 ms cap: it completed 22,257 of 34,360 iterations (34,359 before
rounding). That game was a candidate win.

Both controller runs and wrappers exited 0. Full manifests, executable and
binding snapshots, logs, search/game CSVs, and PGNs are retained in the two
output directories shown above. Both frozen reference hashes were verified
after the runs; candidate hashes and engine-source hashes match across runs.

## Assessment

The exposure feature has not demonstrated an incremental improvement over
MVV-LVA. Its direct-comparison point estimate is unfavorable, although this
small experiment does not establish inferiority either. The positive point
estimate against zccpw does not isolate exposure's contribution; that engine
also differs in MVV-LVA ordering. Retain `mvvlva-exposure` as an experimental
candidate and keep the existing engines unchanged. These results do not
support promoting this penalty as an improvement, automatically enlarging
the test pool, or claiming equal-wall-time strength.

## Validation

All 40 native CTest tests and all five canonical-workflow Python tests passed,
without skips. Native verification accepted all 64 frozen suite positions.
Exposure regression tests cover unprotected and exposed captures, friendly
attackers and defenders, both colors, pawn attack direction, attacks revealed
by source departure, smaller potential losses, equal/favorable exchanges,
quiet moves, promotions, timeline arrivals, unmeasured cross-timeline threats,
check-bonus compatibility, score bounds, and cancellation. CLI checks accepted
seed/widening options and rejected invalid widening values and rollout-only
options. The workflow tests exercise both default and explicitly selected
frozen references.

# Promotion gain and defence-aware exposure

Experiment started on October 21, 2026. This updates `mvvlva-exposure` in place;
the `mvvlva` and zccpw engines remain unchanged. Promotion gain and local
defence were bundled at the user's request, so the experiment measures their
combined effect rather than attributing results to either feature alone.

## Implementation

The existing exposure penalty remains
`min(128, max(0, arriving_value - victim_value) / 10)`, but now applies only
when the arriving piece is attacked and has no friendly geometric defender
on the resulting board. One `board::attacks_to()` query supplies both colors.
The arriving piece does not defend itself, and the departing piece has
already been removed. Pinned defenders may count; this is not a legal
recapture search. Cross-timeline attacks and defenders remain unmeasured.

Physical and arriving promotion coordinates receive
`min(48, max(0, promoted_value - pawn_or_brawn_value) / 10)`, including quiet
promotions. The cached resulting piece determines the promotion type, with
the existing material values for queen, knight, and other configured choices.
Non-promoting moves do not receive this bonus. Departures and null coordinates
continue to score zero.

The exposure cap of 128 and promotion cap of 48 were fixed before testing.
All check behavior and the +300 bonus, progressive widening (2, 0.5), and
the win/loss/zero default policy are unchanged. The bounds preserve ordering
groups: quiet checking promotions score at most 348, captures at least 352,
non-checking captures at most 631, and checking captures at least 652.
There are no rollouts or exchange-sequence evaluations.

## Validation and provenance

The Release `engine-testing` preset was built using GCC 15.2.0, LTO and
native `-fno-rtti`, with the Python binding's required RTTI exception. All 40
native CTest tests and all five canonical-workflow tests passed without skips.
Native suite verification accepted all 64 frozen positions. Regression tests
cover defenders of both colors, exposed/undefended captures, vacated source
squares, pawn direction, quiet/capturing promotions, black promotions, and
knight underpromotions from both pawns and brawns, alongside score bounds,
check compatibility, arrivals/departures and cancellation.

The previous exposure engine was preserved before implementation in
`logs/canonical/reference-exposure-v1/`; its executable SHA-256 is
`8e7cd986723ef96155a0b410ac74d1887ec80b1092f87ebba6a670a95d61d50d`.
Both experiments below compare against the unchanged frozen MVV-LVA reference
in `logs/canonical/reference-mvvlva/`, SHA-256
`20aed5def0f9a6c41b9377079254d339b041be1feaea93f4b8d962ff0076629a`.

The new candidate executable SHA-256 is
`dbc82fcd7f2a40cacfed9ea19939ad44b5367b70821399139bd6167a09a5dc82`;
the engine-source hash is
`f24c8a22ace5c15600e85ebbf52ce49af02c81006b78f1901d4bd9b9fd6402b3`.
The same candidate binary and Python rules binding are snapshotted for both
runs. No implementation changes or parameter tuning were made between runs.

## Canonical screen

```sh
python3 scripts/canonical_test.py run \
  --engine 'mvvlva-exposure --pw-constant 2 --pw-alpha 0.5' \
  --reference logs/canonical/reference-mvvlva \
  --output logs/canonical/mvvlva-exposure-v2-standard-20261021
```

The standard p01–p08 subset was played with reversed engine assignments and
shared seeds, ratio 1, one worker, 200 ms reference searches, a 2000 ms maximum
adaptive target cap, and a 160-action game cap.

Result: **6 wins, 2 draws, 8 losses** (43.75%). The eight pairs contained one
win, five ties, and two losses. The always-valid procedure was **inconclusive**:
superiority p = 0.8333, noninferiority p = 0.7373, inferiority p = 0.8681.
Aggregate target/reference throughput was 1.008; the matched-position
geometric ratio was 1.002. There were no controller, protocol, or legality
failures, no void games, and no action-limit draws. One target search hit its
401 ms cap (pair 3, game 6, action 6): 16,230 of 22,400 requested iterations.
Controller and wrapper both exited 0.

## Longer ratio experiment

Following the inconclusive screen, the user requested a longer run with
`scripts/iteration_ratio_match.py`. The canonical wrapper already uses this
controller; this fallback changes sample size and opening coverage, while
retaining its always-valid statistical method and 0.05 significance threshold.

The direct run uses the frozen confirm p01–p16 opening cycle, fresh seeds
starting at 20262021, at most 128 color-reversed pairs (eight opening cycles),
ratio 1, and `--advance-on better`. Superiority or inferiority may stop early;
noninferiority alone does not count as proof of improvement. Reference time,
target cap, action cap and worker count remain the same as the screen.

This is a separate experiment series. P-values are not pooled with the
screen or earlier experiments. Repeated positions with new seeds measure
search variability over these sixteen fixed positions, rather than adding
new position diversity on each cycle. Results concern this suite and budget;
they do not establish equal-wall-time strength.

Local evidence is stored under
`logs/canonical/mvvlva-exposure-v2-ratio-long-20261021/`, with the full command
and configuration in `run.json`, the launcher in `launcher.py`, immutable
candidate/rules/opening snapshots, complete `controller.log`, game and search
CSVs, PGNs, and `summary.json`. A repeat requires a fresh output directory.

### Longer-run result

The run reached its predeclared limit of **128 pairs / 256 games**, without an
early statistical decision. Result: **109 wins, 17 draws, 130 losses**, a
**45.8984%** score. The pairs contained 22 wins, 73 ties, and 33 losses.

The always-valid result remained **inconclusive** at alpha 0.05:

| Test | Anytime p | Maximum e-value |
| --- | ---: | ---: |
| Superiority | 0.694444 | 1.440000 |
| Noninferiority, 10 percentage-point margin | 0.124810 | 8.012178 |
| Inferiority | 0.140075 | 7.139025 |

No e-value reached the decision boundary of 20. The run ended at its planned
sample limit, rather than stopping at a favorable result. The screen and
long-run p-values were not combined.

| Position cost group | Wins | Draws | Losses |
| --- | ---: | ---: | ---: |
| Cheap | 45 | 0 | 51 |
| Moderate | 39 | 2 | 55 |
| Expensive | 25 | 15 | 24 |
| Total | 109 | 17 | 130 |

Aggregate candidate/reference throughput was 0.995 (109,266.6 versus 109,863.8
iterations/second); the matched-position geometric ratio was 1.003. These
include differences in the positions each ordering searches, rather than
isolating the cost of evaluating the new features.

There were no controller, protocol, or legality failures, no void games, and
no action-limit draws. Ten of 3,618 target searches (0.28%) did not complete
their full iteration budgets. The individual shortfalls are retained in
`statistics.json`; they qualify comparisons at the nominal iteration ratio.
The controller and wrapper both exited 0.

Pair counts, scores and statistical evidence were reproduced from the saved
game CSVs using the controller's unchanged statistical functions and saved
in `statistics.json` for both experiments. All three frozen reference binaries
were hash-verified after testing. Current engine sources match the recorded
engine-source hash, and both experiments used the identical candidate executable.

## Assessment

The implemented bundle has **not demonstrated an improvement over MVV-LVA**.
The longer run's point estimate is unfavorable, but the configured test did
not establish inferiority either. It also did not establish noninferiority;
these results should not be described as proof of equality.

Keep `mvvlva-exposure` available as the requested experimental implementation,
with `mvvlva` unchanged as the comparison baseline. The evidence does not
justify promoting this bundle as stronger, and does not identify whether
promotion gain, defence gating, the retained exposure penalty, or their
interaction explains the observed outcomes.

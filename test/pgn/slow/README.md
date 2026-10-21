# Slow legal-action searches

`flat-ucb-match-123.5dpgn` preserves the position after 69b from Elo match 123,
run `20261022-013651-331857`. White's next search requested 200 ms but did not
reply before the controller's 2.2-second timeout.

The submitted position has 59 timelines and 365 historical boards. HC
construction produces 116 dimensions and 58 initial hypercuboids. It is slow
before the first legal action is found; this is not a verified mate fixture.

Source diagnostics:

- `logs/elo-runs/20261022-013651-331857/match-000123/protocol-failure-0123.txt`
- `logs/elo-runs/20261022-013651-331857/match-000123/go-metrics.csv`

This subdirectory is excluded from the top-level PGN perft test glob. Keep
these positions for explicit performance experiments rather than ordinary
short-running test suites.

## Two-minute first-action experiment

Measured on October 22, 2026, using the native Release core in `build-gcc`
(Homebrew GCC 15.3, probe compiled with `-O3 -DNDEBUG`). Each method ran in a
fresh process, sequentially, and received 120 seconds after PGN parsing and
`HC_info::build_HC` had completed. The probe requested only the generator's
first action and would independently validate any action returned with
`state::can_apply`.

| Method | Result within 120 seconds |
| --- | --- |
| `HC_info::search` | No action; no exhaustion result |
| `HC_info::stable_search` | No action; no exhaustion result |

Both processes were terminated at the experiment limit. Their recorded
`exit_code=-9` denotes this deliberate termination, not an observed engine
crash. Parsing took 37–44 ms and HC construction 1.7–2.1 ms; neither is part of
the 120-second search allowance. The timeout is a lower bound on time to the
first action or exhaustion, not evidence that the position is checkmate.

Raw process outputs and elapsed times are preserved in
`flat-ucb-match-123.timings.json`.

## Random HC ordering experiment

`HC_info::search(space, random_HC_ordering(info.get_universe(), rng))` was
also tested with `std::mt19937` seeds 7, 42, and 123. Each ordering was built
once per search. All three searches reached a 120-second wall limit without
yielding their first action or reporting exhaustion. No action was available
for independent legality validation.

These screenings ran as three concurrent native processes, each with its own
120-second allowance after parsing, HC construction, and ordering setup. They
are not isolated CPU benchmarks directly comparable to the sequential runs
above. The compiler, optimization flags, and fixture were unchanged.

`random_HC_ordering` shuffles coordinate priorities within each HC. This
experiment did not shuffle the search-space hypercuboids or resample the
ordering on each candidate. Random coordinate ordering did not resolve this
case for these seeds and limits; the result is not a proof of mate.

Raw results: `flat-ucb-match-123.random-hc-order.timings.json`.

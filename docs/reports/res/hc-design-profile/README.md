# HC design profile measurements

These selected measurements support the
[HC design report](../../hc-design-profile.md):

- `core.csv`: repeated core-search comparisons, including the later in-place
  removal trials (`trial >= 3`).
- `refined.csv`: repeated engine/fine-tree comparisons with setup, isolation,
  normalization, and destruction counters.

Times are milliseconds. Timeouts are soft budgets checked between attempts, so
individual operations and cleanup can overshoot. The report explains which
phases are included in each comparison and which historical policies were used.

Path columns preserve identifiers recorded during the experiments. Historical
`test/pgn/slow/` paths now correspond to `test/pgn/gigantic/`. The historical
`positions/` inputs for matches 59 and 72 are identical to the permanent
[match 59](../../../../test/pgn/gigantic/factored-match-59.5dpgn) and
[match 72](../../../../test/pgn/gigantic/factored-match-72.5dpgn) fixtures.

For current profiling commands and workloads, see the
[profiling guide](../../../usage/tools.md#hc-search-profiling).

# All-HC, codimension ≤1 measurements

These selected measurements support the
[HC design report](../../hc-design-profile.md):

- `core.csv`: 72 trials covering six spaces, four policies, and three repetitions.
- `paths.csv`: 240 engine/fine-tree trials. Fine-tree elapsed time includes
  destruction; the report subtracts each trial's `destroy_ms` before taking the
  median.
- `weights.csv`: separate `100timelines` measurements of coordinate scoring and
  weighted-order preparation, without search.

Times are milliseconds. The all-HC variant always cuts the selected HC;
secondary cuts use codimension at most one. Fine-tree and engine propagation
settings differ as described in the report, so their columns do not isolate a
single shared change.

Path columns retain historical identifiers. `test/pgn/slow/` is now
`test/pgn/gigantic/`; the old match 59 and 72 input paths refer to the permanent
[match 59](../../../../test/pgn/gigantic/factored-match-59.5dpgn) and
[match 72](../../../../test/pgn/gigantic/factored-match-72.5dpgn) fixtures.

For current profiling commands and workloads, see the
[profiling guide](../../../usage/tools.md#hc-search-profiling).

# Borrowed phantom check detection

`state::has_phantom_check()` replaces the check-only expression
`state.phantom().find_checks(!state.get_present().second).first().has_value()`.
The three mate-classification branches and `game::get_match_status()` now use it.

`check_position::for_phantom(s)` copies only timeline metadata. For every endpoint
whose color matches the **stored player**, it adds a borrowed pointer to that
same board at `next_turn(endpoint)`. Other endpoints remain unchanged. All
additions are installed before querying, allowing checks between newly exposed
boards. This exactly mirrors the board layout of `state::phantom()` without
copying history vectors or constructing any boards.

Unlike HC candidate validation, phantom detection includes physical checks.
The stored player must be used even when `apparent_present()` has advanced to
the opponent during an action that has not yet been submitted.

## Usage

For a boolean query, prefer the state wrapper:

```cpp
if (s.has_phantom_check()) {
    // Classify checkmate/softmate, once legal-action availability is known.
}
```

For individual witnesses, complete check sets, or board lookup:

```cpp
const auto phantom = check_position::for_phantom(s);
const bool attacker = !s.get_present().second;
auto witness = phantom.first_check(attacker);
auto checks = phantom.checks(attacker);
```

Keep `s` alive and unchanged while using the borrowed view. Enumeration order may
differ from general move generation. The factory still produces a check-oriented
view; it does not calculate a new present, timeline status, or move legality.

The UI's `game::get_phantom_boards_and_checks()` uses an owning `state::phantom()`
to export the boards referenced by checking moves in `multiverse::get_boards()`
order.

## Measurements

2026-09-12, macOS 15.7.9 arm64, AppleClang 17.0.0.17000603, Release with core LTO,
`VERIFY_CHECK_POSITION=OFF`. These compare the original clone-and-generate
expression against the new helper in the same executable. Each value is the
median of seven batches of 2,000 queries, with both paths warmed beforehand and
measurement order alternating. Construction and destruction of the temporary
state/view are inside the timed loop; fixture construction is outside it.
Raw batch measurements are in [phantom-benchmark.csv](phantom-benchmark.csv).

| Case | Original, µs/query | Borrowed, µs/query | Speedup |
| --- | ---: | ---: | ---: |
| Standard starting position, no check | 2.017 | 0.422 | 4.78× |
| Synthetic five-timeline history, 645 boards, no check | 4.389 | 1.038 | 4.23× |
| Physical check | 0.586 | 0.133 | 4.42× |
| Check between two newly exposed boards | 0.791 | 0.084 | 9.37× |

These are focused query measurements, not end-to-end engine speedups.
`55332.cpp` remains a regression test for action enumeration; it is not a
representative benchmark of phantom-check usage.

Reproduce with the existing Release build configuration:

```sh
cmake -S . -B /tmp/5dchess-check-bench \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ \
  -DTEST=ON -DVERIFY_CHECK_POSITION=OFF
cmake --build /tmp/5dchess-check-bench --target test_phantom_checks -j 6
/tmp/5dchess-check-bench/test/test_phantom_checks --benchmark
```

## Validation

- All 29 CTest tests pass with `VERIFY_CHECK_POSITION=ON`. The option now also
  checks each `has_phantom_check()` result against the original expression.
- Randomized differential tests compare complete phantom check sets for both
  colors on 120 generated multiverses, before and after simultaneous additions.
  They also compare board contents and verify that pointers borrow the original
  boards and the base remains unchanged.
- Directed tests cover physical checks, checks between newly exposed boards,
  white/black ply transitions, partial actions, pre-submit stored/apparent player
  disagreement, and odd/even timeline conventions.
- `55332`, `test_check_position`, and `test_phantom_checks` also pass with
  AddressSanitizer and UndefinedBehaviorSanitizer, with no diagnostics.
  Leak detection was disabled because it is unsupported on this host.

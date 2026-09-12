# Borrowed HC check detection

Measured on 2026-09-12 using `test/55332.cpp`, unchanged. Every execution
successfully enumerated exactly 55,332 legal actions.

Baseline: `50ab68280ac44bf9d8340b399bf8baa7a02542d9` on branch `temp-state`.
Build: AppleClang 17.0.0.17000603, macOS 15.7.9 arm64, CMake Release (`-O3
-DNDEBUG`), core LTO enabled. Test executable IPO remains disabled by the
existing test configuration. Verification instrumentation is off for timings.

| Version | Median wall time | Range |
| --- | ---: | ---: |
| Baseline clone/replay and general move generation | 0.8173 s | 0.7957–0.8574 s |
| Borrowed candidate view and specialized detection | 0.3807 s | 0.3684–0.4012 s |

This is **2.15× faster**, or **53.4% less elapsed time**, for the whole executable
including parsing, HC construction, and action enumeration. It is not an isolated
microbenchmark of the check detector or a claim about every search position.

One warmup per executable was excluded. Eight measured runs per executable
alternated order; no builds or other validation jobs were running during this
comparison. Raw measurements are in [55332-benchmark.json](55332-benchmark.json).
Before any edits, five baseline runs were 0.797864, 0.801839, 0.806253,
0.807326, and 0.788460 seconds (median 0.801839 seconds).

## Implementation

`HC_info::find_checks` now constructs a borrowed `check_position` using the boards
already held by its selected entries. It allocates one flat array of timeline
metadata, without copying historical timeline vectors, replaying moves, or
recreating resulting boards. All additions are registered before detection.
Branches use their assigned axis coordinate and contain no inherited history.
The original state and HC entries must outlive the view and remain unchanged.

Pure T/L sliders traverse all relevant source squares together as bitboards.
Compound sliders project occupancy and enemy royals using matching copy masks,
then directly intersect sliding attacks with royals. Each direction's board
lookups are shared across its source pieces. Jumps and pawn captures inspect
only their destinations. Physical checks are prefiltered during HC construction;
the detector
also supports physical checks for validation.

The detector returns a concrete checking move for the existing pruning-slice
logic. Check order can differ from the original implementation. General
`state::find_checks` remains unchanged and supplies the independent oracle.

Additional L-direction brawn captures follow `multiverse::gen_moves_impl`,
which enables these captures only in its white pawn/brawn branch.

## Validation

- All 28 CTest tests pass with `VERIFY_CHECK_POSITION=ON`. Every HC candidate
  reaching check detection is independently cloned and replayed; its complete
  check set is compared with the new detector's set.
- `test_check_position` compares both colors, physical and superphysical checks,
  and first-check witnesses against general move generation on 120 deterministic
  random multiverses, before and after simultaneous additions. It covers all
  piece families, unmoved flags, varied board sizes, and uneven timeline bounds.
- Explicit cases cover new boards checking each other, absent branch history,
  reserved but uncreated lines, pure T rays longer than seven steps, occupied
  blockers, and filling a missing intermediate board to open a ray.
- Debug builds with `-O1 -fsanitize=address,undefined -fno-omit-frame-pointer`
  and the replay oracle pass both `55332` and `test_check_position`, with no
  sanitizer diagnostics.

## Reproduction

Build and save the baseline executable at the baseline commit before applying
these changes:

```sh
cmake -S . -B /tmp/5dchess-check-bench \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ -DTEST=ON
cmake --build /tmp/5dchess-check-bench --target 55332 -j 6
cp /tmp/5dchess-check-bench/test/55332 /tmp/55332-baseline
```

After applying the changes, rebuild using the same configuration and compare:

```sh
cmake --build /tmp/5dchess-check-bench --target 55332 -j 6
python3 tools/benchmark_checks.py /tmp/55332-baseline \
  /tmp/5dchess-check-bench/test/55332 --runs 8 --output /tmp/55332-final.json
```

Use a separate build for the expensive replay oracle:

```sh
cmake -S . -B /tmp/5dchess-check-verify \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ \
  -DTEST=ON -DVERIFY_CHECK_POSITION=ON
cmake --build /tmp/5dchess-check-verify -j 6
ctest --test-dir /tmp/5dchess-check-verify --output-on-failure -j 4
```

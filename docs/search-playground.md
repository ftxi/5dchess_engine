# Search playground experiments

This document records short search-strength experiments on the `playground`
branch. Results here are exploratory; a change is not considered stronger until
it survives color-swapped games from several starting positions.

## Baselines

The branch was created without the two current leading policies, so the first
step ported their implementation from commits `d13c34f` and `80ad4b1` onto the
newer core:

- `zero-capture`: zero-cutoff MCTS with captures expanded before quiet moves.
- `zero-capture-check-pw`: capture/check ordering plus progressive widening,
  originally with defaults `C = 2` and `alpha = 0.5`.

The port includes HC-backed move features, check scoring, promotion-aware
cached boards, command-line selection, and policy tests. It does not change the
policies' intended scoring or widening behavior.

### Preliminary measurements

Eight 30 ms, 120-action-cap standard games were color dominated: White won all
eight, leaving both engines 4-4. This sample cannot estimate relative strength.

Ten fixed-position 200 ms probes from the original implementation produced:

| Position | `zero-capture` mean IPS | capture/check PW mean IPS |
| --- | ---: | ---: |
| Initial position | 41,209 | 42,961 |
| After `1.e4 f6` | 28,811 | 24,821 |

After `1.e4 f6`, capture ordering selected ten different quiet moves across the
seeds, while capture/check PW selected only `Bb5` or `Qg4`. The ordering is
therefore affecting the search as intended, but the throughput cost depends on
the position.

## Experiment: cached ordering ranks

`capture_pw_uct_tree_policy::highest_ordered_child()` previously found each
coordinate's rank using a linear scan. The playground version builds an inverse
rank table once, making later rank lookup constant time. It also returns an
already-complete ceiling before constructing another ordering cache.

These changes preserve the random ordering and selected moves for a fixed
iteration budget. A ten-run, 100 ms initial-position probe measured 7,729 IPS
before and 7,664 IPS after, a 0.8% decrease that is within run noise. The
opening is too narrow to exercise the avoided repeated scans, so this is a
complexity improvement rather than a demonstrated playing-strength gain.

## Ordering/widening ablation

The initial leaders confounded two changes, so the playground adds the missing
members of this 2x2 ablation:

| Ordering | No progressive widening | Progressive widening |
| --- | --- | --- |
| Captures | `zero-capture` | `zero-capture-pw` |
| Captures and checks | `zero-capture-check` | `zero-capture-check-pw` |

At 30 ms on the 4x4 `verysmall` continuation, capture+check without widening
led the four-engine round robin at 16-8. On the larger branched `small`
continuation, capture+check with widening led at 9-3 and beat capture-only 6-0.
Check ordering is useful, while widening interacts strongly with position size;
removing it would overfit the tiny board.

## Widening-constant tuning

Four nearby settings (`C=1`, `C=3`, `alpha=0.4`, and `alpha=0.6`) each scored
5-3 against the original `C=2, alpha=0.5` default in the first small sample,
so that result alone was treated as seed-sensitive. `C=1` was selected for a
larger confirmation because it most directly limits early breadth.

Across `verysmall` and `small`, with colors swapped for every seed:

| Time per action | `C=1` | `C=2` | Caps |
| --- | ---: | ---: | ---: |
| 30 ms | 37 | 26 | 1 |
| 100 ms | 16 | 14 | 2 |
| Combined | **53** | 40 | 3 |

The playground default is therefore `C=1`, retaining `alpha=0.5`. This is a
measured short-control improvement, not a final Elo estimate; longer standard
games remain the necessary confirmation.

## Rejected experiments

### Globally most-visited complete action

Selecting the globally most-visited materialized ceiling is theoretically
cleaner than greedy prefix descent, but it lost 12-20 to the existing selector
across 32 paired games. The experiment was removed.

### Victim-valued capture ordering

Adding a victim-class bonus while keeping every capture ahead of quiet checks
finished exactly 16-16 against binary capture ordering across 32 paired games.
It added policy complexity without evidence of strength and was removed.

## Next experiments

### Cache terminal ceiling evaluations

Zero-cutoff search can revisit the same terminal ceiling many times. Cache its
deterministic mate/stalemate reward on the node so repeated visits avoid
reconstructing the state and running terminal action generation.

### Measure the cost of check scoring

Check detection can dominate ordering setup in wide positions. Compare full
capture/check scoring with capture-only scoring on positions of increasing
timeline count, recording both iterations per second and match results. A
faster policy is useful only if the lost tactical guidance does not outweigh
the additional iterations.

## Verification

The port and rank-cache experiment passed these Release-build checks:

```sh
cmake --build build -j 8
./build/test/test_hc_move_evaluation
./build/test/test_mcts_policies
./build/test/test_move_info_evaluation
./build/test/test_check_position
./build/test/test_promotions
./build/test/test_ordering
```

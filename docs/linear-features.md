# Linear evaluation features

The Linear engines evaluate an inconclusive rollout position with 64
player-relative floating-point features:

```text
linear_score = dot(features, weights)
player_score = tanh(linear_score)
white_score = player_to_move == White ? player_score : -player_score
```

The bounded result is compatible with the `[-1, 1]` terminal scores used by
MCTS.

## Layout

| Indices | Count | Features |
|---:|---:|---|
| 0 | 1 | Bias |
| 1–8 | 8 | Mandatory-timeline material sums |
| 9–16 | 8 | Mandatory-timeline material differences |
| 17–24 | 8 | Optional-timeline material sums |
| 25–32 | 8 | Optional-timeline material differences |
| 33–40 | 8 | Unplayable-timeline material sums |
| 41–48 | 8 | Unplayable-timeline material differences |
| 49–61 | 13 | Timeline statistics |
| 62–63 | 2 | Logarithmic move-space volumes |

Material components are ordered as pawn/brawn, knight, rook movement,
bishop movement, unicorn movement, dragon movement, non-royal queen bonus,
and royal membership. Sliding-piece components overlap, so compound pieces
receive the sum of their movement-component weights.

Indices 49–61 follow `timeline_data` declaration order. Index 58 is timeline
advantage (`hostile_created - friendly_created`), so a positive value means
the player to move has more active-timeline allowance.

The move-space values are logarithms of hypercuboid candidate volumes, not
exact legal-action counts. Index 62 includes new-timeline axes; index 63 fixes
those axes at their null coordinate. Summing logarithms avoids overflow for
large multiverses.

## Built-in profiles

`5dchess linear` uses hand-written weights. Mandatory material-difference
weights are `0.05, 0.15, 0.25, 0.15, 0.08, 0.05, 0.45, 0.30`; optional and
unplayable differences use 50% and 15% of those values. Timeline advantage is
weighted `0.25`, and both move-space values are weighted `0.04`. Other
hand-written weights are zero.

`5dchess linear-trained` uses a frozen experimental vector embedded in
`linear.cpp`. It is the first 64 coefficients of the final `r=40` checkpoint;
royal-safety coefficients from that experiment are intentionally excluded.
Consequently this projected profile is not identical to the engine evaluated
in the original matches. See [Frozen Linear profile](linear-trained.md).

Both engines retain the normal rollout option, for example:

```sh
5dchess linear -r 40
5dchess linear-trained -r 40
```

# Linear evaluation features

The Linear engine evaluates an inconclusive rollout position with 84 floating
point features. Features and weights are player-relative, but the final score
is converted to White's perspective for MCTS backpropagation:

```text
linear_score = dot(features, weights)
player_score = tanh(linear_score)
white_score = player_to_move == White ? player_score : -player_score
```

The bounded score is in `[-1, 1]`, matching terminal MCTS scores. A positive
difference feature always means “more friendly than hostile” for the player to
move at the evaluated position.

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
| 64–79 | 16 | Royal-exposure sum/difference pairs |
| 80–83 | 4 | Check sum/difference pairs |

## Material components

Each material block uses this component order:

```text
LPAWN, LKNIGHT, LROOK, LBISHOP,
LUNICORN, LDRAGON, QUEEN, ROYAL
```

The sum counts friendly and hostile component membership. The difference
counts friendly minus hostile membership. Only timeline-end boards with the
block's mandatory, optional, or unplayable status are included.

Sliding-piece components overlap by design. A Princess belongs to `LROOK` and
`LBISHOP`; a non-royal Queen belongs to all four sliding components and also
receives the `QUEEN` bonus. This makes effective piece values additive.

The built-in mandatory-timeline difference weights are:

| Component | Weight |
|---|---:|
| Pawn/Brawn (`LPAWN`) | 0.05 |
| Knight (`LKNIGHT`) | 0.15 |
| Rook component (`LROOK`) | 0.25 |
| Bishop component (`LBISHOP`) | 0.15 |
| Unicorn component (`LUNICORN`) | 0.08 |
| Dragon component (`LDRAGON`) | 0.05 |
| Non-royal Queen bonus (`QUEEN`) | 0.45 |
| Royal membership (`ROYAL`) | 0.30 |

Optional-timeline weights are 50% of these values; unplayable-timeline weights
are 15%. All material-sum weights are currently zero.

Examples of effective mandatory-timeline values are:

```text
Rook = 0.25
Princess = 0.25 + 0.15 = 0.40
Queen = 0.25 + 0.15 + 0.08 + 0.05 + 0.45 = 0.98
Royal Queen = 0.25 + 0.15 + 0.08 + 0.05 + 0.30 = 0.83
```

These are heuristic priors, not trained piece values.

## Timeline statistics

Indices 49–61 follow the declaration order of `timeline_data`:

| Index | Feature |
|---:|---|
| 49 | Total timelines |
| 50 | Active timelines |
| 51 | Inactive timelines |
| 52 | Mandatory timelines |
| 53 | Optional timelines |
| 54 | Unplayable timelines |
| 55 | Playable timelines |
| 56 | Friendly-created timelines |
| 57 | Hostile-created timelines |
| 58 | Timeline advantage |
| 59 | Active-timeline allowance |
| 60 | Friendly-created active timelines |
| 61 | Hostile-created active timelines |

Timeline advantage is `hostile_created - friendly_created`; positive means the
player to move has created fewer timelines and therefore has more active-line
allowance. Its built-in weight is `0.25`. Other timeline-statistic weights are
currently zero.

## Move space

| Index | Feature | Built-in weight |
|---:|---|---:|
| 62 | `log_universe_volume` | 0.04 |
| 63 | `log_non_new_volume` | 0.04 |

These are logarithms of hypercuboid candidate volumes, not exact legal-action
counts. `log_non_new_volume` fixes every possible new-timeline axis at its null
coordinate. Logarithms avoid overflow and reduce the influence of very large
multiverses.

## Royal exposure

The eight exposure channels are:

```text
T_PLUS
T_PLUS_HISTORICAL
L_PLUS
L_MINUS
L_PLUS_T_PLUS
L_PLUS_T_MINUS
L_MINUS_T_PLUS
L_MINUS_T_MINUS
```

Each channel contributes two adjacent features:

```text
sum  = log1p(friendly exposure + hostile exposure)
diff = log1p(friendly exposure) - log1p(hostile exposure)
```

The difference weights are `0.08`; the sum weights are zero. L directions are
normalized into the player-to-move's White-oriented frame before aggregation.

## Checks

| Index | Feature | Built-in weight |
|---:|---|---:|
| 80 | Friendly + hostile checks | 0 |
| 81 | Friendly − hostile checks | 0.25 |
| 82 | Friendly + hostile strong checks | 0 |
| 83 | Friendly − hostile strong checks | 0.40 |

A strong historical check has at least two distinct source pieces on the same
historical source board. Positive difference weights reward pressure on the
opposing royal and penalize equivalent danger to friendly royals.

## Defaults and custom weights

`linear_engine::default_weights()` constructs the built-in profile. Passing a
weight vector to the C++ constructor replaces the complete profile; omitted
entries are not merged with defaults. The command-line `linear` engine uses the
built-in profile, or a schema-checked checkpoint supplied with `--weights`.
See [Training the linear evaluation](linear-training.md) for the optimizer,
sample contract, persistence formats, and command-line workflow.

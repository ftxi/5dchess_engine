# Linear evaluation features

The Linear engines evaluate a nonterminal position with 70
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
| 64–69 | 6 | Royal-safety differences |

Material components are ordered as pawn/brawn, knight, rook movement,
bishop movement, unicorn movement, dragon movement, non-royal queen bonus,
and royal membership. Sliding-piece components overlap, so compound pieces
receive the sum of their movement-component weights.
All differences are from the stored player's perspective. In particular,
unplayable timelines have the opposite endpoint colour; their endpoint-relative
statistics are negated before entering the feature vector.

Indices 49–61 follow `timeline_data` declaration order. Index 58 is timeline
advantage (`hostile_created - friendly_created`), so a positive value means
the player to move has more active-timeline allowance.

The move-space values are logarithms of hypercuboid candidate volumes, not
exact legal-action counts. Index 62 includes new-timeline axes; index 63 fixes
those axes at their null coordinate. Summing logarithms avoids overflow for
large multiverses.

The six royal-safety features are active and inactive royal-ring pressure,
active and inactive physical king-escape scarcity, latent royal threats, and
historical king-square vulnerability. Each is bounded to `[-1, 1]` and is the
opponent's worst royal danger minus the moving player's worst royal danger.
Inactive membership uses the active timeline interval, independently of whose
turn an endpoint belongs to. Royal queens participate in danger measurements;
physical escape scarcity currently measures royal kings only.

Ring pressure counts excess hostile geometric attackers over non-royal friendly
defenders on neighbouring landing squares. Physical escape tests relocate the
king before testing local attacks, and contribute only under pressure or a
threat. Latent threats reuse the existing local threat geometry. Historical
vulnerability examines adjacent landing squares on the preceding same-colour
board when the current royal square was not previously occupied by that side's
royal. These are heuristics: they do not prove legal recaptures, safe 5D escapes,
or a complete Tchaikovsky tactic.

## Built-in profiles

`5dchess linear` uses hand-written weights. Mandatory material-difference
weights are `0.05, 0.15, 0.25, 0.15, 0.08, 0.05, 0.45, 0.30`; optional and
unplayable differences use 50% and 15% of those values. Timeline advantage is
weighted `0.25`, and both move-space values are weighted `0.04`. Other
hand-written weights are zero, including the six appended safety coefficients.

`5dchess linear-trained` uses a frozen experimental vector embedded in
`linear.cpp`. It is the first 64 coefficients of the final `r=40` checkpoint;
royal-safety coefficients from that experiment are intentionally excluded.
The unplayable-material coefficients are sign-converted to preserve the frozen
model's predictions with the corrected player-relative material features.
Consequently this projected profile is not identical to the engine evaluated
in the original matches. See [Frozen Linear profile](../reports/linear-trained.md).

Both engines retain the normal rollout option. From the repository root:

```sh
./build/5dchess linear -r 40
./build/5dchess linear-trained -r 40
```

## Position evaluation with fixed move ordering

`linear-pw`, `linear-safety-pw`, and `linear-trained-pw` use the same Solid
ordering and progressive widening as `mvvlva-exposure`. They have no
rollouts and accept the same `--pw-constant` and `--pw-alpha` options. Before
evaluating a leaf, they establish whether a legal action exists, preserving
terminal win/loss and stalemate scores. Their position features do not affect
move ordering.

`linear-pw` uses the existing handwritten coefficients on indices 0–63, with
zero safety coefficients. `linear-trained-pw` uses the existing frozen
64-coefficient profile, also with zero safety coefficients. `linear-safety-pw`
adds safety coefficients `0.35, 0.15, 0.25, 0.10, 0.20, 0.25`, in declaration
order, to the handwritten profile. These are experimental starting weights.
The first 64 features retain their original scales; the appended six are
normalized as described above. All profiles apply `tanh` after the weighted
sum. Large raw scores can therefore saturate the bounded result.

For an iteration-matched comparison, use the ratio matchmaker with
`mvvlva-exposure` as baseline and one of these three engines as target. This
holds ordering and rollout behaviour fixed while changing position evaluation.

## Smaller feature subsets

The three no-rollout Linear PW engines accept `--evaluation-features SUBSET`.
This masks the selected profile's coefficients without changing the retained
coefficients, feature scales, terminal handling, or move ordering. The default
`all` preserves the complete profile; `none` gives zero nonterminal evaluation.
Use `linear-safety-pw` when testing subsets containing royal-safety terms.

| Subset | Enabled coefficients in the handwritten safety profile |
|---|---:|
| `mandatory-material` | 8 mandatory material differences |
| `material` | 24 material differences across all three timeline categories |
| `material-timeline` | 24 material differences and timeline advantage |
| `material-volume` | 24 material differences and both log volumes |
| `material-safety` | 24 material differences and six safety terms |
| `safety` | 6 safety terms |
| `timeline` | Timeline advantage only |
| `volume` | Both logarithmic candidate-action volumes |
| `timeline-volume` | Timeline advantage and both log volumes (3 coefficients) |

For example:

```sh
./build/5dchess linear-safety-pw --evaluation-features material
```

The vector still has 70 entries, and extraction is unchanged; disabled
coefficients are zero. The handwritten 64-feature profile has 27 nonzero
coefficients, and its safety extension has 33. These subset experiments compare
the contribution of fewer scoring terms, not an optimized extraction cost.
Results depend on the current coefficients; a weak subset does not establish
that the corresponding features cannot help after calibration.

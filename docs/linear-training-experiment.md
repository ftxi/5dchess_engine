# Linear training experiment: 2026-08-19

This report records a time-bounded first training and playing-strength
evaluation of the framework in `src/engine/linear_training.*`. The conclusion
is deliberately conservative: the optimizer learns the held-out game labels,
and a distribution-matched `r=40` checkpoint beats the hand weights at the
same rollout limit. At equal time it is approximately even with the much more
expensive hand-weight `r=120` configuration, but the sample is still too small
and biased to change the built-in defaults.

## Dataset

The source corpus was the 30 completed Standard games in
`logs/standard-linear-mcts-r120-500ms-20260819`, pairs 02 through 16. These
games were produced by the hand-written Linear evaluator against MCTS.

- Training: pairs 02–12, 22 games, 1,011 post-action positions.
- Validation: pairs 13–16, 8 games, 356 post-action positions.
- Training outcomes: 11 White wins, 8 Black wins, 3 action-cap draws.
- Validation outcomes: 6 White wins, 2 Black wins, no draws.

The split was made by complete seed pair before extracting positions. Thus the
two color orientations of a paired game cannot leak between training and
validation. The initial position was omitted. Every game received total
sample weight one, divided equally among its positions, so an 80-action game
did not outweigh a 15-action game.

The generated files are:

```text
logs/linear-training-experiment-20260819/train.data
logs/linear-training-experiment-20260819/validation.data
```

This remains a small, biased corpus: it contains one variant, paired games are
often similar, results include action-cap draws and `nobestmove`
adjudications, and the validation set is only eight decisive games.

## Offline rounds

Metrics below are computed on the untouched validation games. `Sign` is the
weighted fraction of decisive positions for which prediction and eventual
player-to-move result have the same sign. Log loss interprets
`(1 + prediction * target) / 2` as the probability assigned to the observed
winner.

| Round | Training target / initialization | Best epoch | MSE | MAE | Sign | Log loss |
|---|---|---:|---:|---:|---:|---:|
| Baseline | Hand-written weights | — | 0.953702 | 0.793779 | 59.85% | 0.724097 |
| 1 | Outcome, default init, L2 `1e-3` | 106 | 0.779104 | 0.840250 | 77.11% | 0.580488 |
| 2 | Outcome, default init, L2 `1e-4` | 42 | 0.791508 | 0.846622 | 77.81% | 0.600058 |
| 3 | Outcome, lower LR, L2 `1e-4` | 157 | 0.795254 | 0.848779 | 77.14% | 0.596365 |
| 4 | Outcome, zero init, L2 `1e-4` | 127 | 0.780389 | 0.842907 | 77.87% | 0.581746 |
| 5 | 10% outcome + 90% baseline teacher, no L2 | 47 | 0.895362 | 0.789376 | 64.21% | 0.666380 |
| 6 | 25% outcome + 75% baseline teacher, no L2 | 46 | 0.834718 | 0.785279 | 64.77% | 0.641054 |
| 7 | 50% outcome + 50% baseline teacher, no L2 | 148 | 0.781006 | 0.812707 | 66.78% | 0.607560 |

The pure-outcome rounds substantially reduce MSE and log loss but worsen MAE.
They do this mainly by reducing confidence: mean absolute validation
prediction falls from `0.529` to about `0.26`. More seriously, zero-centered
L2 erases sparse hand-written tactical priors. In round 1, the strong-check
weight at feature 83 moves from `0.4` to approximately zero, and several
material weights change sign. Better label prediction therefore does not imply
better move selection.

Rounds 5–7 use a conservative teacher blend

```text
training_target = alpha * game_outcome
                + (1 - alpha) * baseline_prediction
```

and disable zero-centered L2. Round 5 makes the smallest update and is the
only checkpoint that showed a positive aggregate score in more than one match
setting. Its checkpoint is:

```text
logs/linear-training-experiment-20260819/round5-blend-10.weights
```

Round 6 has the best validation MAE, but its direct-evaluation match result was
poor. This is another warning against selecting weights from offline loss
alone.

## Paired engine matches

Each match pair used the same trained and baseline RNG seeds twice, swapping
colors. Games were capped at 60 actions; caps count as draws. The score is
`(wins + 0.5 * draws) / games` from the trained checkpoint's perspective.

### Aggressive outcome checkpoint (round 1)

With `rollout-max-actions=0`, 50 ms per action:

```text
trained 1 win, baseline 6 wins, 1 draw: trained score 18.75%
```

This checkpoint clearly damages playing strength despite its 18.3% held-out
MSE reduction.

### Conservative checkpoint (round 5)

| Evaluation setting | Time/action | Games | Trained W–L–D | Trained score |
|---|---:|---:|---:|---:|
| Direct evaluation, `r=0` | 50 ms | 20 | 12–7–1 | 62.5% |
| Short rollout, `r=40` | 100 ms | 12 | 6–2–4 | 66.7% |
| Corpus rollout, `r=120` | 200 ms | 8 | 2–3–3 | 43.8% |

The first two aggregates are encouraging, but White won 17 of 20 direct games
and the samples are small. Looking at color-swapped pairs is less impressive:
most pairs split by color. At the corpus rollout length, where the evaluator
is called only after an inconclusive 120-action random rollout, the apparent
advantage disappears. These results are not sufficient to promote round 5 to
the built-in default.

Round 6 was also checked:

```text
r=0:  0 wins, 7 losses, 5 draws (20.8%)
r=40: 2 wins, 3 losses, 3 draws (43.8%)
```

Its better offline MAE did not translate into stronger play.

## Distribution-matched cutoff pilot

Round 8 addressed the largest distribution mismatch directly. From each
on-policy corpus position, a random rollout was run for 120 actions. A sample
was retained only when that rollout reached the action limit—the condition
under which the Linear engine actually calls `evaluate()`. Each retained
cutoff state was labeled by the mean White score of eight additional random
rollouts capped at 240 actions.

- Cutoff training set: 70 states from the 22 training games.
- Cutoff validation set: 21 states from the 8 validation games.
- Training labels: 555 of 560 continuation rollouts were conclusive.
- Validation labels: 166 of 168 continuation rollouts were conclusive.
- The seven inconclusive continuations used the baseline prediction as a
  fallback.

With default initialization, no L2, learning rate `0.001`, batch size `16`,
and early stopping, epoch 44 was selected:

| Cutoff validation metric | Baseline | Round 8 |
|---|---:|---:|
| MSE | 0.319583 | 0.112316 |
| MAE | 0.444243 | 0.275921 |
| Sign accuracy | 67.11% | 83.66% |
| Log loss | 0.534172 | 0.507372 |

The offline result is strong but rests on only 21 validation states. Paired
`r=120` matches did not confirm a playing-strength improvement:

| Match batch | Time/action | Action cap | Trained W–L–D | Score |
|---|---:|---:|---:|---:|
| First 6 pairs | 200 ms | 60 | 3–2–7 | 54.2% |
| Next 4 pairs | 150 ms | 80 | 3–5–0 | 37.5% |
| Descriptive total | mixed | mixed | 6–7–7 | 47.5% |

Because the two batches use different controls, the total is descriptive, not
a single homogeneous match. Both batches agree that a clear advantage has not
been demonstrated. The experimental checkpoint is

```text
logs/linear-training-experiment-20260819/round8-cutoff.weights
```

## Rollout-length sweep: rounds 9–11

The cutoff collector was generalized to `r=80`, `r=40`, and `r=30` and run
with five worker threads. The corpus split, at-most-32 source positions per
game, equal total weight per contributing game, exact action-limit filter, and
eight 240-action continuation labels were held fixed. Smaller limits retain
many more states from the same source games:

| Cutoff | Training states | Validation states | Conclusive labels | Fallback labels |
|---:|---:|---:|---:|---:|
| 120 | 70 | 21 | 721 | 7 |
| 80 | 181 | 56 | 1,890 | 6 |
| 40 | 398 | 114 | 4,091 | 5 |
| 30 | 458 | 144 | 4,813 | 3 |

Every sweep fit used the default weights as initialization, no L2 penalty,
learning rate `0.001`, batch size `16`, seed zero, at most 400 epochs, and
early-stopping patience 50. The metrics are weighted by game, as in the data
files.

| Cutoff | Best epoch | Model | MSE | MAE | Sign | Log loss |
|---:|---:|---|---:|---:|---:|---:|
| 120 | 44 | Hand / trained | 0.319583 / **0.112316** | 0.444243 / **0.275921** | 67.11% / **83.66%** | 0.534172 / **0.507372** |
| 80 | 50 | Hand / trained | 0.485872 / **0.149854** | 0.572284 / **0.330017** | 61.99% / **78.27%** | 0.659630 / **0.542336** |
| 40 | 16 | Hand / trained | 0.458407 / **0.209749** | 0.546303 / **0.349797** | 60.78% / **75.33%** | 0.613651 / **0.586406** |
| 30 | 32 | Hand / trained | 0.374566 / **0.135322** | 0.517657 / **0.288682** | 61.04% / **78.59%** | **0.590368** / 0.594391 |

Five additional `r=40` fits were run concurrently with shuffle seeds 1–5.
Their validation MSE occupied the narrow range `0.210075`–`0.211113`, versus
`0.209749` for seed zero; selected epochs ranged from 11 to 16. Thus the
offline result is not a lucky shuffle, although this does not address corpus
bias.

### Matched games

Unless marked otherwise, sweep games used 100 ms per action. Every match used
a 60-action game cap, fixed engine-specific RNG seeds, and color-swapped
pairs. Four games were executed concurrently. Caps count as draws.

| Trained configuration | Opponent | Games | Trained W–L–D | Score |
|---|---|---:|---:|---:|
| `r=80`, round 9 | Hand weights, `r=80` | 8 | 1–4–3 | 31.3% |
| `r=40`, round 10 | Hand weights, `r=40` | 20 | **12–1–7** | **77.5%** |
| `r=30`, round 11 | Hand weights, `r=30` | 8 | 2–2–4 | 50.0% |
| `r=40`, round 10 | Hand weights, `r=120` | 12 | 5–6–1 | 45.8% |
| `r=40`, round 10, 100 ms | Vanilla MCTS, `r=120` | 20 | 5–11–4 | 35.0% |
| `r=40`, round 10, 1,000 ms | Vanilla MCTS, `r=120` | 20 | **9–4–7** | **62.5%** |

The `r=40` same-limit result is the first convincing playing-strength signal
in this experiment. Of ten color-swapped pair scores, eight favored the
trained weights and two tied; none favored the hand weights. An exploratory
two-sided sign test over the eight non-tied pairs gives `p=0.0078`. This is
useful evidence, not a final estimate: positions arise from one small corpus,
the match contains only ten independent seed pairs, and the checkpoint was
chosen after looking at earlier experiments.

The cross-limit match answers the cost question more directly. Over 531
searches at equal nominal move time, the trained `r=40` side averaged 24.47
iterations and 237 iterations/s; the hand-weight `r=120` side averaged 17.19
iterations and 165 iterations/s. That is about 42% more iterations per move.
Linear evaluation itself averaged 2.37 ms per `r=40` search, only about 2.2%
of observed wall time. The 5–6–1 match score does not show that `r=40` is
stronger than `r=120`, but it does show approximate parity in this small test
while making substantially more tree-search progress.

A separate ten-pair match tested the deployment candidate against vanilla
MCTS at `r=120`, under the same 100 ms and 60-action controls. Linear scored
5–11–4, or 35.0%. Three pair scores favored Linear, six favored MCTS, and one
tied; a two-sided sign test over the nine non-tied pairs gives `p=0.508`, so
20 games do not establish a statistically decisive gap. The point estimate
nevertheless favors MCTS. Linear averaged 21.83 iterations and 211
iterations/s, versus MCTS's 15.33 and 145, while evaluator work occupied only
2.24 ms per Linear search. More iterations did not compensate fully for the
shorter rollout and learned cutoff evaluation in this sample.

At 1,000 ms per action, a separate ten-pair sample reversed the point
estimate: Linear scored 9–4–7, or 62.5%. Five pair scores favored Linear,
three favored MCTS, and two tied. The two-sided sign-test result is only
`p=0.727`, so this is not persuasive evidence of superiority. One extra pair
was run to keep four games concurrent; it favored Linear and made the
descriptive 22-game total 10–4–8 (63.6%).

The longer control also reversed the throughput comparison. Across the first
20 games, Linear averaged 97.93 iterations and 97.12 iterations/s per search;
MCTS averaged 115.88 and 115.00. Linear evaluation itself remained cheap at
22.98 ms, about 2.3% of search wall time. The rollout counters explain part
of the result: 90.4% of MCTS's reported rollouts concluded before `r=120`,
whereas only 36.6% of Linear's rollouts concluded before `r=40`; Linear
therefore evaluated many cutoff states and averaged 23.52 rollout actions per
iteration, versus 19.71 for MCTS. In this position sample, reducing the hard
limit did not reduce average rollout work once early conclusions were
included.

The sweep checkpoints are:

```text
logs/linear-training-experiment-20260819/round9-cutoff-r80.weights
logs/linear-training-experiment-20260819/round10-cutoff-r40.weights
logs/linear-training-experiment-20260819/round11-cutoff-r30.weights
```

## Two-second on-policy improvement round: 2026-08-20

To raise root visit counts, 12 new color-swapped games were generated at
2,000 ms per action using round 10 at `r=40` against MCTS at `r=120`. Four
games ran concurrently. Linear scored 7–2–3 in these data-generation games;
because their positions were subsequently used for fitting, this is not an
unbiased evaluation result.

Complete seed pairs 1–4 formed the new training split and pairs 5–6 formed the
new validation split. The same exact-cutoff collector and eight 240-action
continuation labels were used:

| Split | Source games | Cutoff states | Conclusive labels | Fallback labels |
|---|---:|---:|---:|---:|
| New training | 8 | 130 | 1,038 | 2 |
| New validation | 4 | 42 | 336 | 0 |

The new training states were combined with the previous 398 states, producing
528 samples. Five fine-tunes compared combined versus new-only data and
learning rates from `1e-4` to `1e-3`, all initialized from round 10. The
combined-data `1e-3` fit was selected because it gave the safest result across
both validation generations. It selected epoch 3 and stopped after epoch 53.

| Validation set | Metric | Round 10 | Improved checkpoint |
|---|---|---:|---:|
| New 2-second games | MSE | 0.317335 | **0.150324** |
| New 2-second games | MAE | 0.415029 | **0.289782** |
| New 2-second games | Sign | 51.05% | **70.65%** |
| New 2-second games | Log loss | 0.713566 | **0.596090** |
| Previous games | MSE | 0.209749 | **0.205123** |
| Previous games | MAE | 0.349797 | **0.348576** |
| Previous games | Sign | **75.33%** | 71.91% |
| Previous games | Log loss | **0.586406** | 0.594333 |

The new model generalizes much better to the new games without sacrificing
old-set MSE or MAE, although old-set sign accuracy and log loss regress. This
mixed result is why playing tests, rather than MSE alone, determine promotion.

At 2,000 ms/action, four color-swapped pairs against round 10 produced 2–0–6
for the improved checkpoint, a 62.5% score. Two pair scores favored the new
checkpoint and two tied; none favored round 10. Only two games were decisive,
so this is a positive screen rather than strong evidence.

An independent four-pair match against MCTS `r=120` finished 4–4 with no
draws. Two complete pairs favored Linear and two favored MCTS. Improved Linear
averaged 385.49 root iterations and 192 iterations/s per decision; MCTS
averaged 348.73 and 174. Thus the 2-second control delivered hundreds of root
visits and about 10.5% more iterations for Linear, but the eight-game result
establishes only parity on those seeds.

The improved experimental checkpoint is:

```text
logs/linear-r40-improvement-20260820/combined-lr1e-3.weights
```

## Final one-second freeze matches

Before freezing the experiment, three larger independent matches were run at
1,000 ms per action and a 60-action game cap. Each used fixed engine-specific
seeds, color-swapped pairs, and four concurrent games.

### Against hand-weight Linear `r=40`

The planned 20 pairs included one startup timeout; that orientation was rerun
with a larger protocol grace period, and two complete confirmation pairs were
added while keeping the CPUs occupied. The resulting 22-pair, 44-game match
finished:

```text
trained 17 wins, hand weights 20 wins, 7 draws
trained score: 46.6%
```

Eight pair scores favored the trained checkpoint, seven favored the hand
weights, and seven tied. An exact paired score-permutation test gives
`p=0.753`; there is no detectable strength difference. Search workload was
also essentially equal: 142.49 mean iterations for the trained model versus
139.95 for the hand model.

### Against MCTS `r=120`

The fixed 20-pair, 40-game match finished:

```text
trained Linear 23 wins, MCTS 10 wins, 7 draws
trained score: 66.25%
```

Eleven pair scores favored Linear, three favored MCTS, and six tied. A plain
sign test over the 14 non-tied pairs gives `p=0.057`; an exact paired
score-permutation test, which also uses whether a pair was swept or won
1.5–0.5, gives `p=0.0394`. A game-level exact test over the 33 decisive games
gives `p=0.0351`. The paired score estimate is approximately 66.3%, with a
rough pair-level 95% interval of 53.3%–79.2%. This is moderate evidence—not a
precise strength estimate—that trained Linear is stronger under this exact
one-second control.

MCTS averaged 113.88 iterations per decision, while Linear averaged 88.77;
Linear evaluation averaged 22.33 ms. Thus the result was not caused by more
tree iterations: the trained cutoff evaluation appears to compensate for the
shorter rollout despite about 22% fewer iterations in these games.

### Hand-weight Linear `r=40` against MCTS `r=120`

The previously missing direct 20-pair, 40-game match finished:

```text
hand-weight Linear 18 wins, MCTS 7 wins, 15 draws
hand-weight Linear score: 63.75%
```

Eleven pair scores favored hand Linear, four favored MCTS, and five tied. The
exact paired score-permutation test gives `p=0.0510`, just outside the usual
5% threshold; the simpler pair sign test gives `p=0.118`, while a game-level
test over 25 decisive games gives `p=0.0433`. The rough pair-level 95% score
interval is 52.2%–75.3%. This is borderline evidence in isolation, but its
direction and effect size independently agree with the trained checkpoint's
MCTS match.

Hand Linear averaged 83.50 iterations per decision and MCTS averaged 106.90;
hand Linear evaluation averaged 23.77 ms. As in the trained match, Linear won
more games despite performing fewer tree iterations.

Taken together, the defensible final ordering at 1,000 ms is:

```text
trained Linear r=40 ≈ hand-weight Linear r=40 > MCTS r=120
```

The two independent Linear-versus-MCTS samples scored 66.25% for trained
Linear and 63.75% for hand Linear, while their direct match scored 46.6% for
the trained model with `p=0.753`. The training session therefore did not
demonstrate an improvement over the hand-written weights. The advantage over
MCTS comes from the `r=40` Linear cutoff-evaluation approach, not from a
demonstrably better learned coefficient vector.

## Interpretation

The framework itself behaved correctly: optimization converged, early
stopping restored the selected checkpoint, results were stable across shuffle
seeds, held-out metrics improved, saved weights loaded into the engine, and
color-swapped matches ran successfully. The limiting factor is now chiefly
the quantity and diversity of distribution-matched data.

The evaluator is normally applied to states reached after a random rollout,
not to the on-policy game positions used in rounds 1–7. Training on eventual
outcomes of game positions therefore creates a distribution mismatch. Round 8
samples the correct call site, and rounds 9–11 repeat that method at cheaper
cutoffs. The denser 398/114-state `r=40` dataset is the only one so far to
produce a clear same-limit improvement over the hand weights. The two-second
round extends it with new on-policy data and positively screens against round
10. Its initial 4–4 MCTS result was inconclusive; the final one-second matches
show both trained and hand-weight Linear outperforming MCTS, while their
17–20–7 direct match shows no benefit attributable to training. The small
corpus can still let correlated material and timeline features replace sparse
tactical priors without enough evidence.

The next useful experiment should:

1. scale the cutoff collector to many thousands of states from the exact
   point where `linear_engine::default_policy()` calls `evaluate()`;
2. label cutoff states with more longer rollouts or a stronger search, rather
   than the outcome of a different game trajectory;
3. regularize toward the hand-written checkpoint,
   $\lVert w-w_{\text{default}}\rVert^2$, instead of toward zero;
4. include more games, variants, time-travel positions, draws, and balanced
   outcomes; and
5. run at least hundreds of independent color-swapped pairs for `r=40` versus
   both `r=40` and `r=120`, using offline loss only as a screening metric.

The two-second improvement checkpoint is frozen as the final reproducible
experimental candidate:

```sh
build/5dchess linear \
  --rollout-max-actions 40 \
  --weights logs/linear-r40-improvement-20260820/combined-lr1e-3.weights
```

The final one-second match does not justify replacing the hand-written
weights, which remain the safer default.

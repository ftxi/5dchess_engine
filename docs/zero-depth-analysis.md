# Zero engine depth at 1500 ms

This note measures search depth for the `zero` engine in the recent 1500 ms
matches stored in `logs/elo-factored-workspace.sqlite3`. Search depth is not
the length of `mcts_score detailed`: that field describes the selected root
action's fine-tree prefix, not the deepest path visited by MCTS.

## Definitions

The measurements use two related depths for each evaluated MCTS path:

- **Action depth** is the number of ceiling nodes on the path. Depth 1 means
  that the engine evaluated the position after the current action, depth 2
  includes the opponent's reply, and so on.
- **Fine depth** is the number of fine-tree edges from the search root to the
  evaluated ceiling. **Fine-tree height** is the largest such depth
  materialized by the end of the search. In every sampled run, maximum
  evaluated fine depth and materialized tree height were equal.

For an action position, let `P` be the number of playable timelines and `B`
the number of those timelines that contain at least one possible departure.
The hypercuboid has one axis for every playable timeline and one possible-new-
timeline axis for every branch-capable departure timeline. Consequently the
number of fine levels needed to reach the next ceiling is exactly

```
D = P + B, where 0 <= B <= P.
```

For a path crossing several actions, fine depth is the sum of their `D`
values. This explains why fine height is more stable than action depth as the
timeline count grows: fewer actions are completed, but each completed action
is taller. It also explains why fine height is not independent of timeline
count.

## Method

A temporary standalone probe used the production `uct_tree_policy`,
`zero_default_policy`, and `sum_backpropagation`. For each iteration it ran
selection, completed the selected partial path to a ceiling, evaluated it,
backpropagated the zero/terminal reward, and counted both kinds of depth.

The probe was run with three fixed seeds. Isolated probing has higher IPS than
the tournament because it excludes protocol and concurrent-match effects, so
the main stage table caps the probe at the actual tournament median iteration
count for the corresponding sequential action number. The recent data contain
61 completed games involving `zero` and 789 searches by `zero`.

Tournament throughput at 1500 ms was:

| Statistic | Iterations | IPS |
|---|---:|---:|
| p10 | 1,625 | 1,083 |
| median | 3,442 | 2,294 |
| p90 | 11,787 | 7,858 |

The large spread is mostly position width. Median iterations by total timeline
count fell from 11,965 at one timeline to 3,750 at two, 2,394 at three, 1,853
at four, and 1,236 at five.

## Depth by game stage

`Action 21` means the next search after 10 complete sequential White/Black
turns. Time-travel notation's local move number is not used for this count.

| Position | Actual median iterations | Playable timelines in probe | Action depth p50 / p90 / max | Fine depth p50 / p90 | Fine-tree height |
|---|---:|---:|---:|---:|---:|
| Start, action 1 | 14,417 | 1 | 3 / 3 / 3 | 5 / 5 | 7 |
| After 5 full turns, action 11 | 3,542 | 2 | 1 / 2 / 2 | 4 / 10 | 12 |
| After 10 full turns, action 21 | 2,159 | 3 | 1 / 1-2 / 2 | 6 / 6-10 | 12-14 |
| After 15 full turns, action 31 | 2,171 | 2 | 1-2 / 1-2 / 2 | 4-8 / 4-10 | 8-12 |

The ranges at 10 and 15 turns are from two natural positions with the same
stage and playable-timeline count. They show that timeline count alone does
not determine depth: legal-action geometry and the order in which UCT happens
to exhaust fine prefixes matter considerably.

At start position, action depths 2 and 3 first appeared at iterations 21 and
901. Action depth 4 appeared only around iteration 19,400 in isolated runs,
above the tournament p90 of 15,408 start-position iterations. Thus the
practical 1500 ms start-position depth is 3 future actions, not 4.

## Width sample after 10 full turns

The following are natural match positions, each searched for an isolated 1500
ms. Values are medians across three seeds. They compare timeline widths rather
than identical board geometry, so they are descriptive rather than a controlled
causal experiment.

| Playable timelines | Iterations | Action depth p50 / max | Fine depth p50 / p90 | Fine-tree height |
|---:|---:|---:|---:|---:|
| 1 | 3,692 | 2 / 3 | 6 / 8 | 10 |
| 2 | 4,241 | 1 / 2 | 4 / 10 | 12 |
| 3 | 3,994 | 1 / 2 | 6 / 12 | 14 |
| 4 | 1,836 | 1 / 1 | 8 / 8 | 8 |
| 5 | 1,702 | 1 / 1 | 10 / 10 | 10 |

One of the three five-timeline seeds reached depth 2 at iteration 1,391 and
therefore fine height 20; the other two remained at height 10. This boundary
effect is why maximum action depth and maximum fine height can jump even when
iteration counts are close.

## Interpretation

At 1500 ms, `zero` is a practical multi-action search only while the position
is narrow. At start position it reliably evaluates three completed actions.
By two or three playable timelines, most paths end after the current action,
although a meaningful tail reaches the reply. At four or more playable
timelines, the sampled searches usually do not cross the first ceiling.

Fine-tree height is the better continuous description of work done. Across the
after-10-turn width sample, typical height stayed around 8-14 even while
maximum completed-action depth fell from 3 to 1. However, a fine level is one
hypercuboid coordinate decision, not a chess ply, and the cost and branching
of those decisions are not uniform. Both action depth and fine height should
therefore be logged when comparing search policies.

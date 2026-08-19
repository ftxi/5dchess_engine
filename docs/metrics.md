# Search metrics

The MCTS-family engines (`mcts`, `zero`, and `linear`) emit one
`info mcts_stats` line after every completed search. `autoplay.py` copies each
`key=value` pair into the metrics CSV. Blank CSV fields mean that the selected
engine does not emit that metric; notably, `flat-uct` has its own smaller
`info flat_uct_stats` record.

Metrics describe completed iterations only. A rollout interrupted by `stop`
is the final in-flight operation, is not backpropagated, and is not included in
the iteration hierarchy.

## Count hierarchy

```text
iterations (= root visits)
├── terminal_tree_evaluations
│   ├── unique_terminal_tree_nodes
│   └── terminal revisits [derived]
└── rollouts
    ├── conclusive_rollouts
    │   ├── rollout_white_wins
    │   ├── rollout_black_wins
    │   └── rollout_stalemates
    └── inconclusive_rollouts
        └── action-limit cutoffs
```

The exact identities are:

```text
iterations
  = terminal_tree_evaluations
  + conclusive_rollouts
  + inconclusive_rollouts

conclusive_rollouts
  = rollout_white_wins
  + rollout_black_wins
  + rollout_stalemates

terminal revisits
  = terminal_tree_evaluations
  - unique_terminal_tree_nodes
```

`conclusive_rollouts` is determined by how a rollout ends, not by its score.
A stalemate is conclusive even though its score is zero. A Linear action-limit
evaluation is inconclusive even when its heuristic score is nonzero.

## Search totals

| Metric | Definition |
|---|---|
| `elapsed_seconds` | Wall time from the beginning of `find_best_move` through final action selection and metric collection. |
| `iterations` | Completed, backpropagated iterations. Equal to the root node's visit count. |
| `ips` | `iterations / elapsed_seconds`. This mixes expensive rollouts and cheap terminal-tree evaluations, so it is not a rollout-throughput measurement. |
| `conclusive_rollouts` | Rollouts ending in a win, loss, or stalemate. |
| `inconclusive_rollouts` | Rollouts reaching their configured action limit. |
| `terminal_tree_evaluations` | Iterations whose tree policy returned an already terminal tree node, so no rollout ran. |
| `unique_terminal_tree_nodes` | Terminal-tree evaluations for which the node had no previous completed visit. |

## Rollout outcome and workload

| Metric | Definition |
|---|---|
| `rollout_white_wins` | Conclusive rollouts won by White. |
| `rollout_black_wins` | Conclusive rollouts won by Black. |
| `rollout_stalemates` | Conclusive rollouts ending in stalemate. |
| `rollout_actions_total` | Submitted random actions across all completed rollouts. |
| `rollout_actions_max` | Largest number of submitted random actions in one completed rollout. |
| `rollout_seconds` | Total wall time spent inside rollout execution, including move generation and terminal detection. |

Useful derived quantities include:

```text
rollouts = conclusive_rollouts + inconclusive_rollouts
mean rollout actions = rollout_actions_total / rollouts
simulated actions per second = rollout_actions_total / rollout_seconds
```

`rollout_actions_max` is stored because it cannot be reconstructed from totals.
It will equal the configured action limit whenever at least one rollout reaches
that limit.

## Linear cutoff evaluation

These metrics describe scores returned for action-limit cutoffs. For ordinary
MCTS and `zero`, cutoff scores are zero and `evaluation_seconds` is zero.

| Metric | Definition |
|---|---|
| `evaluation_seconds` | Total wall time spent extracting Linear features and evaluating cutoff positions. |
| `cutoff_score_sum` | Sum of cutoff scores. |
| `cutoff_score_abs_sum` | Sum of absolute cutoff scores. |
| `cutoff_score_squared_sum` | Sum of squared cutoff scores. |
| `cutoff_score_min` | Minimum cutoff score, or zero if there were no cutoffs. |
| `cutoff_score_max` | Maximum cutoff score, or zero if there were no cutoffs. |

Using `inconclusive_rollouts` as the sample count, the sums provide the mean,
mean absolute score, variance, and RMS score without recording every sample.

## Tree shape and admission

| Metric | Definition |
|---|---|
| `simulation_tree_depth_sum` | Sum of the evaluated nodes' depths over all completed iterations. Depth counts fine-tree semimove edges from the root. |
| `simulation_tree_depth_max` | Maximum evaluated-node depth. |
| `mcts_nodes_admitted` | Completed iterations in which tree policy admitted one previously unincluded fine node into MCTS. |

One iteration admits at most one MCTS node, so:

```text
mcts_nodes_admitted <= iterations
```

This is called *admitted*, not *expanded*, because fine-tree search may
materialize an entire witness path while MCTS includes only one node from that
path during the iteration. Terminal revisits and evaluations of existing nodes
increase `iterations` without increasing `mcts_nodes_admitted`.

The mean simulation depth is derived as
`simulation_tree_depth_sum / iterations`.

## Root action distribution

A `fine_node` child represents one semimove, while a complete 5D action may be
a path of several semimoves. Root distribution metrics therefore operate on
the first ceiling nodes reachable from the root. Each such ceiling represents
one complete action from the root position. Traversal stops at those ceilings
and does not count actions belonging to later turns.

| Metric | Definition |
|---|---|
| `root_actions_discovered` | Complete root actions whose ceiling nodes have been materialized by the lazy fine tree. This is not necessarily the total number of legal actions. |
| `root_actions_visited` | Discovered root actions whose ceiling node has at least one completed visit. |
| `selected_action_visits` | Visits at the ceiling node for the action actually returned by the engine's existing greedy most-visited descent. |
| `second_action_visits` | Highest ceiling-node visit count among all other discovered root actions. |

The counts at ceiling nodes include simulations that continued into later
turns, because backpropagation passes through the first-action ceiling.

Useful derived quantities are:

```text
root coverage = root_actions_visited / root_actions_discovered
selected share = selected_action_visits / iterations
visit margin = selected_action_visits - second_action_visits
relative visit margin = visit margin / iterations
```

The selected action is determined by greedy descent through the hierarchical
semimove tree. It is therefore possible, though unusual, for another complete
action ceiling to have more visits than the selected ceiling. The metrics
report the implemented selection rule rather than silently replacing it with a
global ceiling-node maximum.

## CSV context

`autoplay.py` records the engine metrics with game and position context:
game/action number, move number, timeline and board counts, color, engine name
and command, requested move time, observed wall time, engine scores, and search
status. This makes each `info mcts_stats` record attributable to the exact
position and engine invocation that produced it.

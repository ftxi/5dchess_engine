# Factored combinatorial MCTS

The `factored` engine is an experimental response to the combinatorial action
space in 5D Chess. If a position has `n` playable timelines and roughly 40
choices per timeline, the number of complete actions grows empirically like
`40^n`. The engine therefore does not enumerate complete actions and does not
run a nested alpha-beta search.

Instead, it augments the existing fine-tree and hypercuboid search with shared
statistics for action coordinates. A legal action can be viewed approximately
as a vector of coordinate choices:

```text
action = (coordinate_0, coordinate_1, ..., coordinate_n)
```

The approximation does not make those choices independent. Hypercuboid search
still constructs and validates every sampled point, removes invalid slices,
and ensures that time-travel arrivals match their departures. The factor table
only shares value evidence after a legal point has been selected.

## Shared evidence

Each nodal action root owns one table indexed by hypercuboid axis and coordinate.
After a simulation, the normal MCTS return is written to every exact fine-tree
node on the path. The same return is also written once to the shared entry for
each coordinate in every traversed action.

Consequently, selecting a coordinate under one prefix teaches the search about
the same coordinate under another prefix. This is most useful on later axes,
where the same move may occur below many different earlier-timeline choices.
The first axis has only one prefix and therefore receives no artificial sharing.

The factor value is not allowed to replace exact branch evidence. For a child
with exact statistics `(S, N)`, the implementation subtracts those observations
from the coordinate aggregate `(S_f, N_f)` and treats the remaining observations
as at most `k` virtual samples:

```text
shared_N = N_f - N
shared_Q = (S_f - S) / shared_N
prior_N  = min(k, shared_N)
Q_blend  = (S + prior_N * shared_Q) / (N + prior_N)
```

The default cap is `k = 2`. As an exact branch accumulates visits, its own mean
dominates this fixed-size prior. If no other prefix has used the coordinate,
`Q_blend` is exactly the ordinary branch mean.

## Expansion and evaluation

Progressive widening retains the playground defaults `C = 1` and
`alpha = 0.5`. When a new branch is requested, coordinate ordering combines:

- a normalized capture/check score;
- the player-relative mean return stored for that coordinate; and
- a confidence ramp that reaches full weight after `k` observations.

The resulting per-axis ordering is passed to hypercuboid search. It proposes a
point but does not bypass any of HC's cross-axis validation or problem-slice
pruning.

At a completed action, `factored` uses a terminal-aware linear default policy:

- checkmate and stalemate retain exact terminal rewards;
- other leaves are evaluated immediately with the hand-written 64-feature
  linear profile; and
- no random rollout or nested adversarial search is performed.

The terminal probe passes its already-built hypercuboid to the move-space
feature calculation, avoiding a second HC construction at every leaf.

The dense value is necessary because sharing a constant-zero nonterminal
reward would convey no useful information between combinations.

## Command line

```sh
./build/5dchess factored --seed 42
./build/5dchess factored --pw-constant 1 --pw-alpha 0.5 --factor-prior 2
```

`--factor-prior` must be positive. Larger values trust cross-prefix evidence
for longer; smaller values make exact branch results take over sooner.

## Current limitations

- The model contains only single-coordinate factors. It has no learned pairwise
  residuals for royal attacks, timeline creation, or other non-additive effects.
- Arrival/departure compatibility is enforced by HC, but their statistical
  values are still stored in separate coordinate entries. Because they always
  co-occur when legal, this acts as a weak implicit binding rather than an
  explicit pair factor.
- Statistics are shared only within one action root. Transpositions between
  equivalent game states are not detected.
- Capture/check ordering and the linear leaf evaluator are both relatively
  expensive. The engine must demonstrate playing strength, not merely better
  value propagation, before becoming a recommended default.
- The search is heuristic. It never labels a selectively searched line as a
  proven mate.

## Verification strategy

Policy tests cover the virtual-sample cap, exact-evidence fallback, coordinate
backpropagation, terminal-aware leaf values, legal move production, and actual
cross-prefix sharing on a branched multi-timeline position. Strength testing
should additionally compare `factored` with `zero-capture-check-pw` and
zero-rollout `linear` at equal wall-clock time across positions with increasing
numbers of playable timelines.

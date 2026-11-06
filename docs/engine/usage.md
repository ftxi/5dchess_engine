# Engine usage

For reproducible engine experiments, use the fixed human-position suite and
the [canonical testing workflow](testing.md).

Available engines include `mcts`, `mcts-weighted`, `zero`, `zero-capture`,
`zero-capture-check-pw`, `mvvlva`, `mvvlva-exposure`, `mvvlva-exposure-pn`,
`mvvlva-pruned`, `mvvlva-solid-only`, `mvvlva-solid`,
`linear`, `linear-trained`, `linear-weighted`,
`linear-pw`, `linear-safety-pw`, `linear-trained-pw`, `neural-pw`, `amaf-pw`,
`flat-ucb`, `flat-ucb-weighted`, and `monkey`. They communicate using the
[5DUCI protocol](../5duci.md).

Moves supplied with `position` are checked against the current position. Initial setups and completed actions must also pass royal-safety
validation. A trailing draft is checked for move applicability; its royal safety
is enforced when `submit` completes the action.
Out-of-bounds squares, missing source boards, historical source boards, empty
source squares, and opponent pieces are rejected without changing the engine's
current position.

The three `*-weighted` engines use move metadata to bias rollout selection
toward tactically promising moves. `zero` is MCTS with a constant-zero default
policy.

`zero-capture` and `zero-capture-check-pw` share move evaluation with different
capture/check weight profiles. `zero-capture-check-pw` also uses progressive
widening; its widening constant and exponent can be changed with
`--pw-constant` and `--pw-alpha`.

`mvvlva` uses the same zero default policy, check detection (+300), and
progressive widening as `zero-capture-check-pw`, including both widening
options (defaults: constant 2, exponent 0.5). It refines capture ordering with
MVV-LVA: prefer the most valuable victim, then the least valuable attacker.
It uses numerical material values and a bounded attacker-rank tie-breaker,
as described under [MVV-LVA experiments](#mvv-lva-experiments). All MVV-LVA
engines use this scoring; the former grouped victim-rank formula is removed.
This is a move-ordering heuristic rather than a static exchange evaluation
and does not use rollouts.

`mvvlva-exposure` adds defence-aware local exposure and promotion gain to
`mvvlva`. For captures whose arriving piece is physically attacked and has
no friendly geometric defender on the resulting destination board, it
subtracts `min(128, max(0, arriving_value - victim_value) / 10)`. Equal/favorable
material exchanges and quiet moves receive no exposure penalty. Promotions,
including non-capturing promotions, add
`min(48, max(0, promoted_value - pawn_or_brawn_value) / 10)`.
Both features use the existing 5D material values and cached result boards.
Their bounds preserve capture/check priority groups; the check bonus stays
+300, and widening options and the zero default policy are unchanged. Attacks
and defenders are geometric, including potentially pinned pieces; a defender
does not establish a legal or available recapture. Cross-timeline exposure
and subsequent exchanges are not measured. The existing `mvvlva` remains
available for comparison.

`mvvlva-exposure-pn` adds the PN-MCTS tree policy to `mvvlva-exposure`.
It keeps the same move ordering, zero leaf evaluation, progressive widening,
and most-visited final move selection. `--pn-constant` controls the proof-number
rank bias (default 1; finite and nonnegative); zero uses ordinary UCT selection.
For example:

```sh
./build/5dchess mvvlva-exposure-pn --seed 42 --pn-constant 0.5 \
  --pw-constant 2 --pw-alpha 0.5
```

The policy follows the [UCT-PN rank formula](https://arxiv.org/abs/2303.09449).
It ranks admitted, visited children by proof number on the root player's
decisions and by disproof number on the opponent's decisions. Equal numbers
share a rank. Terminal wins prove the root player's win goal; terminal losses
and stalemates disprove it. Unsearched continuations remain unknown while
widening is incomplete, so exploring only some moves cannot establish that
all moves win or lose. Cutoff evaluations do not establish proofs. This engine
uses PN information for tree selection; solved subtrees are still searched
and final selection still uses visits.

The Linear engines evaluate inconclusive rollout positions with the same
bounded feature model. `linear` and `linear-weighted` use hand-written
weights, while `linear-trained` uses a frozen experimental profile. See
[Linear evaluation features](linear-features.md).

For comparisons without rollouts, `linear-pw`, `linear-safety-pw`, and
`linear-trained-pw` preserve `mvvlva-exposure` ordering and progressive widening
while replacing its nonterminal zero evaluation. `linear-safety-pw` enables six
additional king-safety features; the other two retain zero coefficients for
those features. These engines do not accept rollout options.

`neural-pw` uses `mvvlva-solid` move ordering and HC construction: numerical
MVV-LVA with exposure and promotion scoring, solid-check pre-pruning, and
optional-timeline move pruning. It uses progressive widening with no rollouts.
Optional-timeline pruning can reduce the legal action set, as described below.
It evaluates only nonterminal positions through a 70-input network with hidden
layers of 32 and 16 ReLU units and a final
`tanh` value. Terminal wins, losses and stalemates retain their exact values.
The model predicts the side-to-move value; the search converts it to its
White-relative reward convention. The network never changes move ordering.

```sh
./build-canonical/5dchess neural-pw --model /absolute/path/to/model.nn \
  --pw-constant 2 --pw-alpha 0.5
```

The model is required and loaded before the protocol handshake. Native
inference needs no additional library. Version 1 is a whitespace-separated
text file beginning with `5DNEURAL 1`, feature schema `linear70-v1-mover`,
and dimensions `70 32 16 1`. It contains 70 training means, 70 inverse
standard deviations, then row-major weights and biases for each layer
(2,817 parameters). Constant training columns use inverse scale zero.
Numbers must be finite; incorrect versions, dimensions, truncated files,
negative scales and trailing content are rejected.

To prepare and train a model with the Python rules binding, NumPy and PyTorch
installed in the Python used for the build:

```sh
python scripts/neural_dataset.py prepare --build build-canonical \
  --output /absolute/path/to/new-dataset
python scripts/neural_train.py --build build-canonical \
  --dataset /absolute/path/to/new-dataset --output /absolute/path/to/new-training
```

The exporter validates each game in an isolated process, records exclusions,
groups related games before splitting, excludes canonical-suite source
families, and deduplicates sampled states. It samples completed played
actions with their original full multiverse history. Missing outcomes and
ambiguous variations are excluded. The trainer fits normalization only on
training data, selects across three seeds using validation loss, evaluates
the selected checkpoint on the test split once, and checks native output
agreement. `model.nn`, its normalization, hashes, learning curves and
metrics are saved in the training output directory. The binding exposes raw
features through `game.get_evaluation_features()` and native model inference
through `neural_model.load(path).evaluate_features(features)`.

`amaf-pw` adds action-local AMAF credit sharing to the MVV-LVA exposure/PW
search, without rollouts. Semimoves under different partial-action prefixes
share reward/count statistics only when they belong to the same completed
action's HC context. By default, shared estimates influence UCT selection among admitted
children; admission and action completion retain MVV-LVA exposure ordering.
They never establish legality or prune a continuation.

```sh
./build-canonical/5dchess amaf-pw --amaf-strength 0.5 \
  --amaf-horizon 32 --amaf-decay inverse --pw-constant 2 --pw-alpha 0.5
```

`--amaf-strength` is a finite mixing amplitude in `[0,1]` (default 1), and
`--amaf-horizon` is a finite nonnegative visit scale (default 32). For direct
child visits `n`, horizon `h`, and strength `a`, the supported decay weights
are `a/(1+n/h)` (`inverse`), `a/sqrt(1+n/h)` (`sqrt`), and
`a*exp(-n/h)` (`exponential`). Strength or horizon zero disables sharing.
The shared mean is used only when it includes more observations than that
child's direct statistics. Initial-axis coordinates have no alternative
prefix within the action, so they need no shared estimate.

`--amaf-credit blend` mixes the shared mean into the direct child value.
`--amaf-credit centered` keeps the direct value and adds a decaying shared
advantage relative to that action's average reward. Both retain the UCT
exploration term; the default is `blend`.

`--amaf-application admission` uses ordinary UCT selection and applies shared
credit when admitting children or completing a partial action.
`--amaf-application both` applies credit at both stages; `selection` is the
default. Admission adds a bounded, mover-relative shared advantage to the
existing MVV-LVA exposure score. Its decay uses shared coordinate visits,
since a new prefix has no direct child history. It keeps every allowed
coordinate and passes the resulting action through normal legality checks.
With absent or all-zero shared evidence, ordering is exactly the original
seeded ordering. Cached rankings update between completed MCTS iterations.

`--amaf-application children` applies shared credit only when choosing among
children already discovered through completed legal actions. New-action
discovery retains the original MVV-LVA exposure ordering, and ordinary UCT
selection is unchanged. Shared marginal credit therefore cannot reorder the
legality search for previously unexamined combinations of semimoves.

`--amaf-evidence unique-leaves` counts each completed leaf node once in shared
AMAF statistics, including when that node is revisited after ignition.
Ordinary UCT rewards and visits still update every iteration. This option
requires `admission` or `children` mode; the default `visits` counts every
completed iteration. All evidence remains scoped to its action context.

Leaves use zero nonterminal values unless `--model /absolute/path/to/model.nn`
is supplied. `--value-scale` scales only the model's nonterminal output by a
finite number in `[0,1]` (default 1). Zero skips neural feature extraction.
Terminal wins, losses and stalemates always retain exact values. `amaf_stats`
protocol information reports sharing activity for diagnostics. The default
settings are configuration defaults, not a strength recommendation.

`--neural-usage leaf` backs up learned nonterminal values through ordinary UCT
and AMAF statistics. `--neural-usage amaf-prior` instead backs up zero for
nonterminal ordinary UCT rewards while using the learned value only for
action-local shared estimates. This keeps the network's influence within the
decaying AMAF term. Exact terminal rewards update both sets of statistics in
either mode. With sharing disabled, `amaf-prior` therefore retains zero leaf
evaluation regardless of model predictions.

`--cache-evaluations` reuses a completed evaluation when the same ceiling node
is revisited. It is optional and applies only to `amaf-pw`, whose zero and
neural leaf policies are deterministic. The cache belongs to the current
search tree, preserves terminal outcomes, and is discarded before the next
search. Interrupted evaluations are never cached. `amaf_stats` includes cache
hit and miss counts; backpropagation still runs once per completed iteration.

The two flat-UCB engines evaluate each legal root action with repeated rollouts
and choose with the adversarial UCB rule, without expanding a search tree.
Weighting affects their rollout moves, not root UCB selection.

Search engines accept an optional unsigned 32-bit seed using `--seed` or `-s`.
For example, use `5dchess flat-ucb-weighted --seed 1234`. MCTS, Linear, and
flat-UCB engines also accept `--rollout-max-actions` (or `-r`) to shorten each
default-policy rollout from its default limit of 200 actions. For example, use
`5dchess linear-weighted --rollout-max-actions 40`.

Weighted engines accept `--weight-temperature` (or `-wt`) to control how
strongly move weights bias their rollouts. Lower positive values increase the
bias; higher values approach randomized selection. The default is `1200`. The
same value can be changed through 5DUCI with
`setoption name weight-temperature value 600.0`.

The rollout action limit can be changed through 5DUCI with
`setoption name rollout-max-actions value 40`. A rollout that reaches the limit
is scored as a draw by MCTS and flat-UCB; Linear evaluates the final rollout
position instead. Setting the limit to zero disables rollout entirely.

## MVV-LVA experiments

`mvvlva` uses numerical victim values with preference for lower-valued attackers,
check ordering, zero rollout evaluation, and progressive widening. `mvvlva-exposure`
also penalizes locally attacked, undefended captures and rewards promotion gain.
These geometric exposure scores do not establish whether a recapture is legal.

The material values used by MVV, exposure, and promotion gain are:

| Piece | Value |
|---|---:|
| Queen | 1440 |
| Princess | 900 |
| Bishop / knight | 350 |
| Rook | 300 |
| Common king | 200 |
| Brawn | 150 |
| Pawn | 100 |
| Unicorn | 60 |
| Dragon | 30 |

For a nonroyal victim, the capture score is
`480 + alpha * victim_value + beta * (maximum_attacker_rank - attacker_rank)`,
where `alpha` is `1/16` and
`beta = alpha * minimum_material_value_gap / maximum_attacker_rank`.
With the current values, `beta` is `3/16`.
Attacker ranks are generated at compile time from distinct material values;
equal values share a rank. Both royals receive the highest attacker rank and
are excluded from victim scoring. The derived LVA step remains a tie-breaker
when the values change.
Checking adds 300. Exposure subtracts up to 128, and promotion gain adds up to
48. These bounds preserve the priority of checking captures over other
captures, and captures over checking quiet promotions. Other weighted
engines retain their grouped capture features.

`mvvlva-solid` adds two HC construction changes to `mvvlva-exposure`:

- Omit physical moves on optional timelines and arrivals onto existing optional
  timelines whose source timeline is also optional. Arrivals from mandatory
  timelines and moves creating new branches remain available.
- Pre-prune coordinates that unavoidably expose a friendly royal. Both options
  use the regular HC builder and its partitions by branch count.

Optional-timeline pruning may reduce the number of legal actions generated.
Check pre-pruning preserves the legal-action set.

If the optional-timeline restriction leaves no legal action, search falls back
to unrestricted move generation for that position. Rules and terminal
adjudication continue to use the complete legal-action space.

For comparisons, `mvvlva-pruned` enables only optional-timeline pruning, and
`mvvlva-solid-only` enables only check pre-pruning. All five engines accept
`--seed`, `--pw-constant` (default 2), and `--pw-alpha` (default 0.5).

```sh
./build/5dchess mvvlva-solid --seed 42 --pw-constant 2 --pw-alpha 0.5
```

## Search limits and stopping

`go movetime <milliseconds>` requests cancellation when the time limit expires,
measured from search start, including setup. Zero or negative limits request
cancellation before search begins. The `stop` command requests cancellation of
the current search through the same mechanism.

Search stops at the next existing cancellation checkpoint. Fine-tree and
hypercuboid operations run uninterrupted, so slow operations and timer scheduling
can cause searches to exceed `movetime`. After cancellation, engines still
select a move from discovered candidates and return `bestmove`, or `nobestmove`
if no candidate is available. A search that finishes early cancels its timer
without waiting for the time limit.

## Autoplay

To play a match between two engines, first [build the Python module and engine](../usage/building.md#python-module), then run `scripts/autoplay.py` with the two engines specified as arguments. Example:
```sh
python scripts/autoplay.py --white "./build/5dchess mcts" --black "./build/5dchess monkey"
```
The root Python scripts live in `scripts/`. The default Python module directory
is the repository’s `build/`, regardless of your current directory. Engine
commands, explicit `--module-dir` values, and log paths are relative to the
current directory. To run from `scripts/`, use:

```sh
cd scripts
python autoplay.py --white "../build/5dchess mcts" --black "../build/5dchess monkey"
```

This writes logs under `scripts/logs/`, which Git ignores.

Autoplay and both matchmakers check engine startup with `5duci` / `5duciok`,
then `isready` / `readyok`, before searching. A missing executable reports the
engine role, command, and working directory, including when CPU pinning is
enabled. An invalid engine name or other startup failure reports the command,
failed handshake step, exit status or timeout, and captured engine output.
The initial stderr error is preserved even if followed by a long usage message,
so a command such as `build/5dchess ghost` includes `Unknown engine: ghost`.
`--timeout` controls how long each handshake response may take.

Autoplay records standard PGN match headers. Use `--event` and `--site` to
name a standalone run; both default to `Autoplay` and `Local`. In a series,
`Round` is the one-based game number.

For a compact 10-game flat-UCB/MCTS protocol smoke test, use:
```sh
python scripts/autoplay.py --white "./build/5dchess flat-ucb --seed 11 --rollout-max-actions 2" --black "./build/5dchess mcts --seed 29 --rollout-max-actions 2" --movetime 20 --max-actions 2 --games 10
```
Autoplay metrics include `engine_score`: flat-UCB's selected-action rollout win rate, or MCTS's average score along the selected principal path. MCTS also fills `engine_scores` with the colon-separated score for each path node. Both engines report `iterations` and `ips` (iterations per second) in the CSV metrics.
Use `--help` for more information on how to set a starting game, time controls, or a multi-game series.

For persistent Elo ratings across many registered engines, manual result
reporting, automatic opponent selection, and concurrent games, see
[Elo registry and concurrent matchmaker](elo-matchmaker.md). The pure
`elo.py` tool never launches an engine; `elo_matchmaker.py` optionally automates
its scheduled matches through `autoplay.py`.

For comparisons at position-scaled iteration budgets, use the
[adaptive iteration-ratio matcher](iteration-ratio-match.md). It pins both
players and the baseline meter to the same worker CPU, refills workers
continuously, and searches for a ratio meeting the noninferiority margin.

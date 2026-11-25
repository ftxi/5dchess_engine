# Engine usage

For reproducible engine experiments, use the fixed human-position suite and
the [canonical testing workflow](testing.md).

Available engines include `mcts`, `mcts-weighted`, `zero`, `zero-capture`,
`mvvlva`, `mvvlva-exposure`, `mvvlva-pruned`, `solid`, `solid-amaf`, `solid-amaf1`, `solid-amaf2`,
`mvvlva-exposure-pn`, `linear`, `linear-trained`, `linear-weighted`,
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
as described under [MVV-LVA scoring](#mvv-lva-scoring). It uses zero leaf
evaluation.

`mvvlva-exposure` uses [Solid scoring](#mvv-lva-scoring) with ordinary UCT.
`mvvlva-pruned` adds optional-timeline pruning. `solid` uses check pre-pruning
and score-weighted UCT exploration.

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

`neural-pw` uses Solid move ordering, weighted exploration, check pre-pruning and
progressive widening. Optional-timeline moves are included by default. Use
`--ordinary-uct` or `--prune-optional-moves` for controlled comparisons.
It evaluates only nonterminal positions through a position network with hidden
layers of 32 and 16 ReLU units and a final
`tanh` value. Terminal wins, losses and stalemates retain their exact values.
The model predicts the side-to-move value; the search converts it to its
White-relative reward convention. The network never changes move ordering.

Deterministic completed-leaf evaluations are cached for the duration of one
search. Repeated visits retain ordinary visit/backpropagation updates. With a
Solid-schema model, leaf evaluation searches for a legal action with optional
moves included. If that search finds no action, it classifies the position as
checkmate or stalemate. The same HC supplies the model's volume inputs. Use
`--no-evaluation-cache` to disable the cache for comparisons.

The default `--neural-usage prior` mode keeps ordinary nonterminal backups at
zero and uses the neural value only as a decaying bias in UCT child selection.
Each completed nonterminal leaf contributes once to that bias. The child mean
is centred against its parent's mean in White's reward perspective, so a
constant White-relative offset across sampled leaves creates no preference.
Terminal wins, losses and stalemates retain their exact backup
values. Semimove ordering and progressive widening are unchanged.

```sh
./build-canonical/5dchess neural-pw --model /absolute/path/to/model.nn \
  --neural-usage prior --neural-strength 0.3 --neural-horizon 64 \
  --neural-decay inverse --pw-constant 2 --pw-alpha 0.5
```

`--neural-decay` accepts `inverse`, `sqrt` or `exponential`. With child visit
count `n`, horizon `h` and strength `a`, the bias weight is respectively
`a/(1+n/h)`, `a/sqrt(1+n/h)` or `a*exp(-n/h)`. Strength is in `[0,1]` and
horizon is nonnegative; zero disables the bias and skips model evaluation. The prior defaults to
inverse decay with strength 0.3 and horizon 64. Use `--neural-usage leaf`
for direct value backup. `--value-scale` scales nonterminal neural values in either mode, with
zero recovering zero nonterminal evaluation at fixed iterations. These controls
configure evaluation guidance, not rollout or terminal rewards.

```sh
./build-canonical/5dchess neural-pw --model /absolute/path/to/model.nn \
  --pw-constant 2 --pw-alpha 0.5
```

The model is required and loaded before the protocol handshake. Native
inference needs no additional library. Version 1 is a whitespace-separated
text file beginning with `5DNEURAL 1`, the feature schema, and the network
dimensions. The capture-gain schema uses `solid87-capture-gain-v6-mover` and
`87 32 16 1`. The local-check schema uses `solid85-local-checks-v5-mover` and
`85 32 16 1`. The protection schema uses `solid83-protection-v4-mover` and
`83 32 16 1`. The ray schema uses `solid80-rays-v3-mover` and `80 32 16 1`.
The 70-input solid and legacy schemas use `70 32 16 1`. The file contains one
mean and inverse standard deviation per input, then row-major weights and
biases for each layer. Constant training columns use inverse scale zero.
Numbers must be finite; incorrect versions, dimensions, truncated files,
negative scales and trailing content are rejected.

The `solid70-v2-mover` schema retains the material, timeline and royal-safety
inputs described in [evaluation features](linear-features.md). Its input 62 is
the log candidate volume of existing playable-timeline axes; input 63 is the log
candidate volume of possible new-timeline axes. Both use the historical
optional-pruned view of the check-pruned HC, including null coordinates that
survive pruning. For these two inputs, optional physical moves and optional-to-optional
nonbranching arrivals are omitted. These are candidate factors, not legal-action
counts. Each factor uses the natural logarithm `log(max(1, volume))`, so an empty factor, a unit factor,
or an absent group of axes gives zero.

`solid80-rays-v3-mover` appends ten features to the solid 70 inputs. In order,
columns 70–74 describe orthogonal rays (rank and file combined), and columns
75–79 describe diagonals. Each group reports empty landing squares up to the
first blocker, friendly first blockers, undefended friendly first blockers,
rays with empty landing squares and no friendly first blocker, and total
material value of friendly first blockers on rays with at least one empty
landing square. The last value is in pawn units using the engine's capture
values, with 20 for a royal blocker. An undefended blocker has no geometric
defender of the same color on that board after excluding the originating royal.
Each feature takes the maximum over each side's royals on the latest boards of
mandatory and optional timelines, then subtracts the mover's maximum from the
opponent's. These are geometric features. Actual jumps and defenses still
require legal actions.

`solid83-protection-v4-mover` appends three inputs to the ray schema. On the
latest boards of mandatory and optional timelines, count nonroyal pieces
geometrically attacked by an enemy on the same board. Assign each target to
its strongest same-board defender tier, in order:

1. Pawn, brawn or common king.
2. Bishop, knight or rook.
3. Princess or queen.

Each target contributes to at most one tier. Royals, unicorns and dragons
provide no protection credit. Unprotected targets contribute zero. The three
inputs are mover counts minus opponent counts, summed across those boards.

`solid85-local-checks-v5-mover` appends two mover-relative counts. The first
counts physical moves that leave an opposing royal under same-board attack on
the resulting board. The second counts jumps with such an attack on either
the arrival or departure board. A jump counts once even if both boards
qualify. The check-pruned hypercuboid includes optional moves for these two
counts. Each underlying move with its configured promotion counts once,
including when several branch axes represent it. These are candidate moves
and can still require compatible moves on other timelines. The earlier volume
inputs continue to use the optional-pruned view.

`solid87-capture-gain-v6-mover` appends the mover's best physical capture
gain and best jump capture gain, in pawn units. For each candidate capture of
a nonroyal piece, take the victim's value and apply Solid's exposure penalty
on the resulting arrival board. The penalty subtracts the arriving piece's
value when it is geometrically attacked and has no friendly defender. Each
input takes the maximum in its move group, with zero as the floor. The two
gains share the local-check feature pass and include optional moves.

Legacy `linear70-v1-mover` models are also accepted and retain their original
unpruned total/playable volume inputs. A schema tag selects feature extraction;
changing the tag on an old checkpoint does not convert the model.

To prepare and train a model with the Python rules binding, NumPy and PyTorch
installed in the Python used for the build:

```sh
python scripts/neural_dataset.py prepare --build build-canonical \
  --output /absolute/path/to/new-dataset
python scripts/neural_train.py --build build-canonical \
  --dataset /absolute/path/to/new-dataset --output /absolute/path/to/new-training
```

To train on scored sibling actions, use the sibling trainer with one or more
judged label panels and SciPy installed alongside PyTorch. Each panel contains
roots, family IDs, successor snapshots, four judge scores per successor, and
the saved 5,000-iteration pair comparisons.

```sh
python scripts/neural_sibling_train.py --build build-canonical \
  --labels /absolute/path/to/first/labels.json /absolute/path/to/second/labels.json \
  --output /absolute/path/to/new-sibling-training
```

The trainer replays successor snapshots, checks labels and groups families
that share positions. It compares an 80-input network with two 87-input
variants, including one with protection tiers masked. Fold zero supplies
the validation positions for all three. The lowest validation pair log loss
selects the exported `model.nn`. The output also contains per-seed models,
validation curves, linear ranking controls, native parity errors, and
`splits.json` for reproducing the family assignments.

To regenerate features without changing an existing dataset's source-family
splits or retained position selection, use `refresh` before training:

```sh
python scripts/neural_dataset.py refresh --build build-canonical \
  --existing /absolute/path/to/old-dataset --output /absolute/path/to/new-dataset
```

Refresh revalidates source files and position identities. Failed games are
recorded in `refresh-exclusions.json`; it does not add replacement positions.
Both `prepare` and `refresh` export the capture-gain schema by default. Use
`--schema` to select one of the earlier input schemas.

For training experiments, `--family-balanced` gives each source-game family
equal total loss weight and fits normalization with the same family weights.
`--weight-decay` sets AdamW weight decay (default `1e-4`). `--feature-set`
accepts `all` (default), `without-rays` (the first 70 inputs), `differences`
(24 material differences, timeline
advantage and six safety differences), or `relative-volume` (those inputs plus
both volume features). Ablations keep the dataset's input count and zero the masked
normalization scales; distinct piece channels are not merged. Use `--defer-test`
to omit test-partition evaluation during model development. In family-balanced
mode, checkpoints are selected using family-weighted validation BCE.

The exporter validates each game in an isolated process, records exclusions,
groups related games before splitting, excludes canonical-suite source
families, and deduplicates sampled states. It samples completed played
actions with their original full multiverse history. Missing outcomes and
ambiguous variations are excluded. The trainer fits normalization only on
training data, selects across three seeds using validation loss, evaluates
the selected checkpoint on the test split once, and checks native output
agreement. `model.nn`, its normalization, hashes, learning curves and
metrics are saved in the training output directory. The binding exposes raw
solid-schema features through `game.get_neural_features()` and ray features
through `game.get_neural_ray_features()`. The protection schema is available
through `game.get_neural_protection_features()` and the local-check schema
through `game.get_neural_local_check_features()`. The capture-gain schema
is available through `game.get_neural_capture_gain_features()`. The legacy linear
inputs remain available through `game.get_evaluation_features()`, and native model
inference through `neural_model.load(path).evaluate_features(features)`.

`amaf-pw` adds action-local AMAF credit sharing to the Solid-scored progressive-widening
search, without rollouts. Semimoves under different partial-action prefixes
share reward/count statistics only when they belong to the same completed
action's HC context. By default, shared estimates influence UCT selection among admitted
children; admission and action completion retain Solid ordering.
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
advantage relative to that action's average reward. `--amaf-credit outside`
blends in the mean from other prefixes, with an additional confidence factor
of `m / (m + 8)` for `m` outside observations. All three retain the UCT
exploration term. The default is `blend`.

`--amaf-application admission` applies shared credit when admitting children or completing a partial action.
`--amaf-application both` applies credit at both stages; `selection` is the
default. Admission adds a bounded, mover-relative shared advantage to the
existing Solid score. Its decay uses shared coordinate visits,
since a new prefix has no direct child history. It keeps every allowed
coordinate and passes the resulting action through normal legality checks.
With absent or all-zero shared evidence, ordering is exactly the original
seeded ordering. Cached rankings update between completed MCTS iterations.

`--amaf-application children` applies shared credit only when choosing among
children already discovered through completed legal actions. New-action
discovery retains the original Solid ordering, and UCT revisits use their usual exploration weights. Shared marginal credit therefore cannot reorder the
legality search for previously unexamined combinations of semimoves.

`--amaf-evidence unique-leaves` counts each completed leaf node once in shared
AMAF statistics, including when that node is revisited after ignition.
Ordinary UCT rewards and visits still update every iteration. This option
requires `admission` or `children` mode; the default `visits` counts every
completed iteration. All evidence remains scoped to its action context.

AMAF engines also support `--mean-reward-selection`. It follows the visited prefix with the best average direct reward for the player making the turn. Equal averages favor more visits, then existing child order. Choose at most one final move selector.

The AMAF engines accept `--completion-aware-v0` to try an experimental final move selector. At each step it considers prefixes within 5% of the most-visited prefix. It switches when another eligible prefix contains a completed move with at least four more visits than the best-supported completion of the leading prefix. The default follows the most-visited prefix at each step.

Leaves use zero nonterminal values unless `--model /absolute/path/to/model.nn`
is supplied. `--value-scale` scales only the model's nonterminal output by a
finite number in `[0,1]` (default 1). Zero skips neural feature extraction.
Terminal wins, losses and stalemates always retain exact values. `amaf_stats`
protocol information reports sharing activity for diagnostics. The default
settings are configuration defaults, not a strength recommendation.

`--neural-usage leaf` backs up learned nonterminal values through ordinary UCT
and AMAF statistics. With a model, AMAF engines default to
`--neural-usage amaf-prior` without quiescence and `--neural-usage leaf` with
quiescence. An explicit `--neural-usage` choice applies in either case.
The `amaf-prior` mode backs up zero for
nonterminal ordinary UCT rewards while using the learned value only for
action-local shared estimates. This keeps the network's influence within the
decaying AMAF term. Use `solid-amaf1 --model /absolute/path/to/model.nn`
for weighted exploration and half-strength pooled credit. Loading a model preserves the preset's AMAF strength and credit formula. Exact terminal rewards update both sets of statistics in
either mode. With sharing disabled, `amaf-prior` therefore retains zero leaf
evaluation regardless of model predictions.

`--cache-evaluations` reuses a completed evaluation when the same ceiling node
is revisited. It is available on all AMAF presets. Both zero and neural leaf policies are deterministic. The cache belongs to the current
search tree, preserves terminal outcomes, and is discarded before the next
search. Interrupted evaluations are never cached. `amaf_stats` includes cache
hit and miss counts; backpropagation still runs once per completed iteration.

With a model, AMAF engines accept `--quiescence-depth 1` through
`--quiescence-depth 4`. For example, add `--quiescence-depth 4` to
`solid-amaf1 --model /absolute/path/to/model.nn --value-scale 0.1 --cache-evaluations`.
Use `--mean-reward-selection` to select the final turn by mean direct reward.
Quiescence refines completed root actions; deeper MCTS leaves use the static
evaluator so that tactical refinement does not consume every tree iteration.
Each tactical ply is one submitted turn. The engine compares captures and checks across
timelines, including non-capturing defenses when a royal is threatened.
It also samples complete turns to find checks that emerge only when several
semimoves are submitted together. This search is selective; budget exhaustion
does not establish checkmate, and a position with no examined reply retains
its static evaluation.
It scores at most 16 additional positions per uncached root action, shares that
allowance across candidates and replies, and scales the resulting leaf score
once. If the move deadline interrupts refinement after static evaluation
finishes, that static score can complete the current iteration; the interrupted
refinement is not cached. Omit the option for the static neural evaluation.

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

## MVV-LVA scoring

`mvvlva` uses numerical victim values with preference for lower-valued attackers,
check ordering, zero rollout evaluation, and progressive widening. The
`mvvlva-*` variants, rollout-free linear presets, `neural-pw`, and AMAF presets
use the same scoring as `solid`.
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

In `mvvlva`, the capture score for a nonroyal victim is
`480 + alpha * victim_value + beta * (maximum_attacker_rank - attacker_rank)`,
where `alpha` is `1/16` and
`beta = alpha * minimum_material_value_gap / maximum_attacker_rank`.
With the current values, `beta` is `3/16`.
Attacker ranks are generated at compile time from distinct material values;
equal values share a rank. Both royals receive the highest attacker rank and
are excluded from victim scoring. The derived LVA step remains a tie-breaker
when the values change.
Checking adds 300. Other weighted engines retain their grouped capture features.

`solid` retains the complete legal-action space and pre-removes unavoidable
royal checks. In `solid` and the `mvvlva-*` variants, a capture scores one quarter of the victim's material value plus
the same small attacker tie-breaker. If the resulting destination square has
an enemy attacker and no friendly defender, it subtracts one quarter of the
arriving piece's value. Check still adds 300, and promotion gain adds up to 48.

Physical moves on initially optional timelines and nonbranching arrivals
between two initially optional timelines receive a 1024-point penalty.
Departures from initially optional timelines receive a 480-point penalty.
An arrival that creates a new timeline receives a 280-point penalty on the new
timeline's axis. A null choice on that axis scores zero. These penalties affect
expansion order. Progressive widening can delay low-priority moves at finite
search budgets.

When revisiting admitted semimoves, `solid` also weights the UCT exploration
bonus using the same scores. Each 256 points below the best visited,
registered sibling halves the base weight. A quiet optional move 1024 points
behind a null choice thus starts with weight 1/16.
Among those siblings, let `k` count semimoves receiving the 1024-point
penalty and `m` count the rest. When `k > m > 0`, `solid` multiplies each
penalized sibling's weight by `sqrt(m/k)`. UCT scales its exploration bonus by the square root of the
resulting weight. Pass `--ordinary-uct` to use unweighted exploration on
`solid`, `neural-pw` or a Solid AMAF preset.

The three AMAF variants share semimove rewards across prefixes of one action
root. During a UCT revisit, let `n` count visits through the current prefix
and `m` count visits to the same coordinate through other prefixes. When
`m > 0`, selection blends the local mean with the estimate below.
At `m = 0`, all three use the local mean.

| Engine | Shared estimate | Weight of shared estimate |
|---|---|---|
| `solid-amaf` | Mean over all `n + m` visits | `32 / (32 + n)` |
| `solid-amaf1` | Mean over all `n + m` visits | `0.5 * 32 / (32 + n)` |
| `solid-amaf2` | Mean over the `m` outside visits | `0.5 * m / (m + 8) * 32 / (32 + n)` |

All three use `solid`'s exploration weights and expansion order. Final move
selection follows the most locally visited child at each prefix.

Fine-tree expansion in `zero` and the progressive widening engines first
searches for a legal continuation of the highest-ranked unexpanded semimove.
When a probe uses up its work budget, the next probe includes one more ranked
semimove.
Each probe shares the learned exclusions with the other probes. A legal
continuation found after widening completes the action immediately. At full
width, expansion can use a retained legal continuation or ordinary Deferred
hypercuboid search with the earlier semimoves still fixed.

`--fine-prefix-budget <n>` sets the work budget per ranked group. It counts
tree visits, temporary search steps, matching, and legality checks. The
default is 256. Use 0 to compare against strict semimove ordering.

```sh
./build/5dchess solid --seed 42 --fine-prefix-budget 128
```

Use `solid --prune-optional-moves` to omit physical moves on optional
timelines and arrivals between existing optional timelines. Arrivals from
mandatory timelines and moves creating new branches remain available.
Optional-timeline pruning may reduce the legal actions generated. If the
restriction leaves no legal action, the search retries with optional-move
pruning disabled. Check pre-pruning preserves the legal-action set.

`mvvlva-solid-only` and `mvvlva-solid` are compatibility aliases for
`solid --ordinary-uct` and `solid --ordinary-uct --prune-optional-moves`,
respectively. The aliases use the current Solid scoring.

```sh
./build/5dchess solid --seed 42 --prune-optional-moves --pw-constant 2 --pw-alpha 0.5
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

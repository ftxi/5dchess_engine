# Engine usage

For reproducible engine experiments, use the fixed human-position suite and
the [canonical testing workflow](testing.md).

There are thirteen engines: `mcts`, `mcts-weighted`, `zero`, `zero-capture`,
`zero-capture-check-pw`, `mvvlva`, `mvvlva-exposure`, `linear`, `linear-trained`, `linear-weighted`,
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
Ranks follow the existing 5D capture-value groups, from low to high:
pawn, brawn, rook/common king, bishop/knight, unicorn/dragon, princess, queen.
Royal queens have queen victim rank; royal attackers rank above queens.
Captures score `480 + 16 * (victim_rank - 1) + (8 - attacker_rank)`, with ranks
starting at one. This retains the existing capture/check priority groups.
Quiet moves have no attacker penalty. This engine adds no attack/defence or
exchange evaluation and does not use rollouts.

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

The Linear engines evaluate inconclusive rollout positions with the same
bounded 64-feature model. `linear` and `linear-weighted` use hand-written
weights, while `linear-trained` uses a frozen experimental profile. See
[Linear evaluation features](linear-features.md).

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

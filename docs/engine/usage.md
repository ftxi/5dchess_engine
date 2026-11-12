# Engine usage

Available engines include `mcts`, `mcts-weighted`, `zero`, `zero-capture`,
`zero-capture-check-pw`, `mvvlva`, `mvvlva-exposure`, `mvvlva-pruned`,
`mvvlva-solid-only`, `mvvlva-solid`, `solid`, `linear`, `linear-trained`, `linear-weighted`,
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

`solid` retains the complete legal-action space and pre-removes unavoidable
royal checks. It uses MVV-LVA exposure ordering with a 1024-point penalty for
physical moves on initially optional timelines and nonbranching jumps between
two initially optional timelines. The arrival receives the jump penalty once.
Branch arrivals retain their normal scores. The penalty affects expansion order,
while search rewards and UCT selection use the usual outcomes. Progressive
widening can delay low-priority moves at finite search budgets.

```sh
./build/5dchess solid --seed 42 --pw-constant 2 --pw-alpha 0.5
```

`mvvlva-solid` is the legacy pruning experiment. It adds two HC construction changes to `mvvlva-exposure`:

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
`mvvlva-solid-only` enables only check pre-pruning. These engines accept
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

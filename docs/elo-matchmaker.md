# Elo registry and concurrent matchmaker

The Elo tools are split so rating management never needs to start an engine:

- `elo.py` owns the engine registry, pairing suggestions, persistent match
  records, result reporting, and Elo calculations.
- `elo_matchmaker.py` is an optional automatic runner. It schedules matches
  through `elo.py` and plays them concurrently through `autoplay.py`.

Both scripts use `logs/elo-matchmaker.sqlite3` by default. Put a custom
`--database` option before the subcommand.

## Register engines

An engine ID denotes one fixed playing policy. Register a new ID after any
change that can affect move selection.

```sh
python elo.py register mcts --command "./build/5dchess mcts"
python elo.py register monkey --command "./build/5dchess monkey"
python elo.py register experimental-v1 \
  --command "./build/5dchess linear --weights weights/v1.bin" \
  --artifact weights/v1.bin
```

Tracked artifacts are hashed at registration. A rated schedule is rejected if
one changes. Create a distinct version with `clone`:

```sh
python elo.py clone experimental-v1 experimental-v2 \
  --command "./build/5dchess linear --weights weights/v2.bin" \
  --artifact weights/v2.bin
```

Add `--inherit-rating` to use the parent's current rating as the new version's
starting estimate. Match histories remain separate.

## Schedule and report games manually

`suggest` is read-only. `schedule` reserves matches and gives them persistent
IDs:

```sh
python elo.py suggest 4 --engine experimental-v2
python elo.py schedule 4 --engine experimental-v2
```

Focused matchmaking defaults to the adaptive strategy:

```sh
python elo.py suggest 4 --engine experimental-v2 --strategy adaptive
```

Adaptive matchmaking favors opponents near the focus engine's Elo, gives more
weight to opponents with established ratings, mildly discourages repeats,
balances opponents above and below the focus rating within a wave, and uses one
bounded exploration game per ten focused games. For a broad diagnostic pass,
the coverage strategy retains the original fewest-head-to-head-first behavior:

```sh
python elo.py suggest 4 --engine experimental-v2 --strategy coverage
```

`--strategy` only applies together with `--engine`. Matchmaking without a focus
engine continues to use the existing global arena algorithm unchanged.

Run each pairing with `autoplay.py` or any other controller, then report the
observed result:

```sh
python autoplay.py --white "WHITE COMMAND" --black "BLACK COMMAND"
python elo.py report 42 white --pgn completed-game.5dpgn
```

Valid reports are `white`, `black`, `draw`, and `void`. A void result is stored
but does not affect ratings. The final result automatically finalizes a
complete batch. Use `--no-auto-finalize` and `elo.py finalize BATCH_ID` when
explicit control is preferable.

## Abort unwanted games and disable broken engines

Abort means that an operator deliberately excludes work from ratings and
matchmaking. It differs from `void`, which means a game ran but did not produce
a valid result. Aborted matches retain their reason and diagnostic artifacts,
but their official result is cleared.

Abort the only open batch, or name a batch when several are open:

```sh
python elo.py abort batch --reason "engine configuration was wrong"
python elo.py abort batch 7 --reason "engine configuration was wrong"
```

This aborts scheduled, running, and reported matches in the batch, applies no
rating changes, and releases the open-rated-batch lock. Running automatic
workers notice the state change, cancel autoplay, and close their engines.

Individual matches can be excluded while keeping the rest of the batch:

```sh
python elo.py abort match 42 44 --reason "bad opening position"
```

If valid reported matches remain after all other work is aborted, the batch is
finalized using only those valid results. If nothing valid remains, the batch
is cancelled. Matches in an already finalized batch cannot be aborted because
later rating snapshots may depend on them.

To remove every open match involving a broken engine:

```sh
python elo.py abort engine experimental-v2 --reason "loads the wrong weights"
```

When that engine occurs in multiple open batches, select one with `--batch` or
explicitly use `--all-open`.

Disabling is deliberately separate from aborting:

```sh
python elo.py disable experimental-v2
```

A disabled engine is excluded from future scheduling, but existing scheduled
matches remain reproducible snapshots and can still be resumed. The command
warns when such matches exist. To disable and abort all its open work in one
operation, use:

```sh
python elo.py disable experimental-v2 --abort-open \
  --reason "engine is broken"
```

Re-enable the same unchanged engine when appropriate. If its playing behavior
was fixed or its weights changed, register or clone a new engine ID instead.

Machine-readable scheduling is available with `--format json` or
`--format tsv`. Other useful commands are:

```sh
python elo.py pending
python elo.py history
python elo.py leaderboard
python elo.py update ENGINE --no-enabled
```

## Rating and matchmaking mathematics

### Elo update

For engine A with rating $R_A$ against engine B with rating $R_B$, A's
expected score is

$$
E_A = \frac{1}{1 + 10^{(R_B-R_A)/400}}.
$$

After the result, A's rating change is

$$
\Delta_A = K(S_A-E_A),
$$

where $S_A$ is 1 for a win, 0.5 for a draw, and 0 for a loss. The default is
$K=32$, configurable with `--k-factor`. B is updated with the complementary
score, so the two changes sum to zero apart from floating-point rounding. Void
games are recorded but have no rating change; the automatic runner reports an
action-limit result as a draw.

The constants follow the conventional Elo scale: a 400-point difference means
the stronger engine has ten times the weaker engine's expected-score odds.
Starting at 1500 is only a neutral pool convention. $K=32$ is large enough for
new engine versions to move usefully over tens of games while avoiding the
large game-to-game swings of a placement-only rating. This implementation is
plain Elo: it does not maintain Glicko-style rating deviation or uncertainty.

Concurrent games form one rating batch. Every game $i$ uses the ratings
captured at the start of the batch, and all changes for engine A are summed:

$$
R'_A = R_A + \sum_i \Delta_{A,i}.
$$

Applying the sum once makes the final rating independent of which concurrent
game finishes first. The next batch is scheduled from the updated ratings.

### Adaptive focused matchmaking

With `--engine F`, `adaptive` is the default strategy. For each eligible
opponent O, ordinary adaptive games maximize

$$
Q(F,O) = P \times H \times C \times L \times B,
$$

using these factors:

| Factor | Formula | Reason for the formula and parameter |
| --- | --- | --- |
| Rating proximity $P$ | $\exp(-|R_F-R_O|/200)$ | Nearby ratings produce the most informative results. A 200-point gap multiplies preference by $e^{-1}$, strongly preferring close opponents without imposing a hard cutoff. |
| Repeat factor $H$ | $(1+h)^{-0.25}$ | $h$ is prior plus already-proposed head-to-head games. The quarter-power is intentionally mild: 15 previous meetings only halve this factor, so a close opponent can remain preferable to an unused distant one. |
| Confidence proxy $C$ | $0.5 + 0.5\min(1,\sqrt{g/40})$ | $g$ is the opponent's historical game count. Established opponents are better rating anchors, but the 0.5 floor prevents new opponents from becoming unselectable. Confidence reaches its cap at 40 games. This is a matchmaking heuristic, not statistical rating deviation. |
| Batch load $L$ | $(1+b)^{-0.5}$ | $b$ is how often the opponent has already been selected in the proposed concurrent batch. The square-root penalty makes a second simultaneous use worth about 0.707 while still allowing repeats when that opponent is clearly best. |
| Bracketing $B$ | 1.15 for the underrepresented rating side; otherwise 1 | If opponents exist both above and below $R_F$, a small 15% bonus favors the side used less in the current batch. This checks both rating directions without overriding a large proximity difference. |

Exact ties are resolved by the seeded pseudo-random generator, so the same
database and `--seed` produce the same suggestions.

Every tenth non-void game involving the focus engine is an exploration game.
Exploration first restricts candidates to $|R_F-R_O|\leq400$, then minimizes
this tuple lexicographically:

$$
(h,\ b,\ g,\ |R_F-R_O|,\ \text{seeded tie-break}).
$$

Here exploration's $g$ includes opponents already proposed earlier in the
current wave. Thus exploration prefers a less-tested nearby opponent, but does
not routinely spend games on extreme mismatches. Ten percent is enough to
detect local rating errors and keep nearby comparison paths connected without
restoring exhaustive round-robin behavior. The 400-point window covers a wide
Elo neighborhood; if that window is empty, the scheduler falls back to the
ordinary adaptive score.

### Coverage focused matchmaking

`--strategy coverage` retains the original diagnostic behavior. It chooses the
minimum tuple

$$
(h,\ |R_F-R_O|,\ g,\ \text{seeded tie-break})
$$

lexicographically. Because $h$ is first, the focus engine is spread across
opponents with the fewest previous meetings before rating proximity matters.
This is deliberately less efficient for estimating Elo, but useful for a new
engine smoke test that should encounter weak, strong, and unusual baselines.

### Global matchmaking without a focus engine

Without `--engine`, the existing global arena scheduler is unchanged. For every
available unordered pair A/B, it minimizes

$$
(\max(g_A,g_B),\ g_A+g_B,\ h_{AB},\ |R_A-R_B|,\
\text{seeded tie-break})
$$

lexicographically. This prioritizes equal test participation, then pair
diversity, then rating proximity. It is intended to maintain a broadly tested
engine pool rather than imitate a player-facing live-game queue.

For rated scheduling, only rated non-void games contribute to $g$, $h$, and
color history. For unrated scheduling, all non-void games contribute. After
each proposed pairing, the in-memory counts are incremented before choosing the
next pairing, so one suggested wave balances itself. Per-engine `max_parallel`
limits are also enforced; zero means unlimited and a stateful engine defaults
to one.

### Color assignment

After selecting two engines, colors are assigned independently of the opponent
score:

1. Give White to the engine that has had fewer White games in this exact
   head-to-head pairing.
2. If tied, give White to the engine with fewer White games overall.
3. If still tied, use the seeded tie-breaker.

These rules reduce color imbalance without distorting which opponents are
selected.

## Run games automatically and concurrently

The automatic runner schedules games in waves. `--jobs` controls actual
concurrency; the default wave size is the same as the job count.

```sh
python elo_matchmaker.py run \
  --engine experimental-v2 \
  --strategy adaptive \
  --event "Experiment A" \
  --games 100 \
  --jobs 8 \
  --movetime 1000
```

The `--strategy` option may be omitted here because focused automatic runs
default to `adaptive`. Use `--strategy coverage` when initially checking a new
engine broadly against every available opponent.

The event name is copied into each game's PGN. The runner records the site as
`Local/Batch N`, the round as the match's one-based ordinal within that batch,
and the global database match number in the `Matchid` header.

Each completed game is printed as one block containing its pairing, result,
full PGN, log path, and metrics path. Because concurrent games can finish in
any order, their Elo changes are printed when the wave finishes.

All games in a wave use the ratings captured when the wave was scheduled.
Their deltas are summed and applied atomically, making the result independent
of process completion order. The next wave uses the updated ratings.

Each result and its completion block are reported as soon as its worker
finishes. Pressing Ctrl+C before the first result cancels the entire wave: it
does not report games, update ratings, or affect future matchmaking, and a new
rated run can start immediately. The batch remains only as cancelled audit
history.

If at least one game has already finished, those finished results are retained
and unfinished games return to the scheduled state. The open batch can then be
resumed without looking up its ID when it is the only open batch:

```sh
python elo_matchmaker.py resume --jobs 8
```

An explicit ID remains available when more than one batch is open:

```sh
python elo_matchmaker.py resume 7 --jobs 8
```

An abrupt termination that prevents this cleanup (for example, a power loss or
`kill -9`) can leave a batch to inspect and resume:

```sh
python elo.py pending
python elo_matchmaker.py resume --recover-running --jobs 8
```

Use `--recover-running` only when the former workers are no longer alive. It
returns their matches to the scheduled state before launching replacements.

## Training and mutable weights

Elo assumes a reasonably fixed player. Games in which an engine learns should
be scheduled as unrated:

```sh
python elo.py register learner --command "..." --training --stateful
python elo_matchmaker.py run --engine learner --games 1000 --unrated
```

A stateful engine is limited to one game in each concurrent wave so two
processes cannot overwrite the same checkpoint. The engine must save weights
to disk because `autoplay.py` starts fresh engine processes for every game.

For rated evaluation, freeze a checkpoint, register it as a non-training engine
version, and run its matches in parallel. Frozen checkpoint versions appear on
the official leaderboard; training engines do not unless
`elo.py leaderboard --include-training` is requested.

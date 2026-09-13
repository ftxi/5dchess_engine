# Elo registry and queued match runner

`elo.py` stores immutable engine versions, match results, and ratings in
SQLite. `elo_matchmaker.py` schedules games and keeps a fixed number of
`autoplay.py` workers busy until its durable queue is empty.

Pass a custom database before the subcommand. The default is
`logs/elo-matchmaker.sqlite3`.

## Register engines

An engine ID identifies one fixed playing policy. Register a new ID whenever
code, options, or weights can affect move selection:

```sh
python3 elo.py register baseline --command "./build/5dchess zero-capture" \
  --artifact ./build/5dchess
python3 elo.py register candidate \
  --command "./build/5dchess zero-capture-check-pw" \
  --artifact ./build/5dchess
```

Artifacts are hashed at registration and checked before rated games start.
Use `clone` to register a changed version. `rename` changes metadata for future
matches when playing behavior is unchanged.

Useful registry commands include:

```sh
python3 elo.py list
python3 elo.py leaderboard
python3 elo.py history
python3 elo.py pending
```

The leaderboard fits all completed rated results together with a
Bradley-Terry model and a weak prior around each registered initial rating.
It also fits White advantage. The result is independent of database row,
scheduling, and game-completion order. A queued batch refreshes stored ratings
from the same fit when it finalizes.

## Run a continuously refilled queue

`--games` is the total queue length and `--jobs` is the maximum number of games
running at once. A free worker immediately claims another game; rating
bookkeeping does not introduce wave barriers.

```sh
python3 elo_matchmaker.py run \
  --games 120 --jobs 6 --movetime 1000 \
  --event "Candidate evaluation"
```

Each result, PGN, autoplay log, and metrics file is saved as soon as its game
finishes. Match commands are copied into the database when scheduled.

Use repeated `--core` options to bind each worker and both engines in its game
to one CPU:

```sh
python3 elo_matchmaker.py run --games 120 --jobs 6 \
  --core 0 --core 1 --core 2 --core 3 --core 4 --core 5
```

Named pools keep hardware classes separate and store the assigned group on
each match:

```sh
python3 elo_matchmaker.py run --games 480 --jobs 12 \
  --core-group P=0,1,2,3,4,5 \
  --core-group E=6,7,8,9,10,11
```

The scheduler cycles each directed pairing through the groups. Choose a queue
size divisible by the number of directed pairings and groups when exact
pair/color/group balance is required.

## Interrupt and resume

Ctrl+C records settled results, returns this runner's unfinished games to the
scheduled state, closes engine processes, and exits. Resume with the same
worker and affinity options:

```sh
python3 elo_matchmaker.py resume --jobs 6 \
  --core 0 --core 1 --core 2 --core 3 --core 4 --core 5
```

After a power loss or forced termination, first ensure the old workers are no
longer running, then reclaim their matches:

```sh
python3 elo_matchmaker.py resume --recover-running --jobs 6
```

The runner pauses on a protocol or worker error and preserves all available
diagnostics. It does not guess which engine is faulty. After inspection,
disable a broken engine and resume; unfinished games involving it are aborted
while completed results remain:

```sh
python3 elo.py disable candidate
python3 elo_matchmaker.py resume --jobs 6
```

To discard every open result involving the engine as well, use:

```sh
python3 elo.py disable candidate --abort-open --reason "broken engine"
```

Re-enable only an unchanged engine. Register a new engine ID after a fix that
can affect play.

## Manual scheduling

`suggest` is read-only. `schedule` creates persistent match IDs for games run
by another controller:

```sh
python3 elo.py suggest 4 --engine candidate
python3 elo.py schedule 4 --engine candidate
python3 elo.py report 42 white --pgn completed-game.5dpgn
```

Valid reports are `white`, `black`, `draw`, and `void`. Void and aborted games
do not affect ratings.

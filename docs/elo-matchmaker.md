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

Machine-readable scheduling is available with `--format json` or
`--format tsv`. Other useful commands are:

```sh
python elo.py pending
python elo.py history
python elo.py leaderboard
python elo.py update ENGINE --no-enabled
```

## Run games automatically and concurrently

The automatic runner schedules games in waves. `--jobs` controls actual
concurrency; the default wave size is the same as the job count.

```sh
python elo_matchmaker.py run \
  --engine experimental-v2 \
  --games 100 \
  --jobs 8 \
  --movetime 1000
```

Each completed game is printed as one block containing its pairing, result,
full PGN, log path, and metrics path. Because concurrent games can finish in
any order, their Elo changes are printed when the wave finishes.

All games in a wave use the ratings captured when the wave was scheduled.
Their deltas are summed and applied atomically, making the result independent
of process completion order. The next wave uses the updated ratings.

Interrupted batches can be inspected and resumed:

```sh
python elo.py pending
python elo_matchmaker.py resume 7 --recover-running --jobs 8
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


# Command-line tools

Build with `TOOLS=ON`; see the [build guide](building.md).

Malformed PGN reports the expected input and the text encountered, for example
`Expected a promotion piece; found end of input.` for an unfinished promotion.

The engine is built as `build/5dchess`, and the general command-line utility is built as `build/5dtools`. For commands that consume a game, provide 5DPGN on standard input and press Control-D to complete it. Current utility commands include:

-  `print [--at <turn>]`: print the selected state of the game
-  `stats [--at <turn>] [--format text|json]`: display position, timeline, and HC-volume statistics
-  `count [--at <turn>] [--format text|json] [<policy>] [<max>]`: display number of available moves capped by `<max>`
-  `all [--at <turn>] [<policy>] [<max>]`: display all legal moves capped by `<max>`
-  `checkmate [--at <turn>] [<policy>]`: determine whether the selected state is checkmate/stalemate
-  `diff [--at <turn>]`: compare the output of two algorithms
-  `perftest [<policy>]`: on each intermediate state, print 1 if it is checkmate/stalemate, 0 otherwise
-  `rollout [options]`: run and report random rollout simulations
-  `replay-log <log-or-snapshot.5dpgn> [seed] [--movetime ms] [--engine name [options...]]`: replay a protocol failure or captured position with configurable search settings; see [search diagnostics](../engine/testing.md#search-deadlines-and-slow-position-traces)

For `print`, `stats`, `count`, `all`, `checkmate`, and `diff`, `--at 0` and
`--at 0b` select the initial
position. A value such as `--at 16b` selects the state immediately after
turn `16b` is submitted. A count equal to its maximum is the cap sentinel;
for example, `max=10001` and a result of `10001` means 10,000 or more
actions.

Legal-action searches account for timelines reactivated by branching. An
action may leave a later board untouched when a reactivated timeline puts the
apparent present on the opponent's turn, including when the action itself
advances that timeline to the opponent's turn.

Checkmate and stalemate are distinguished by simulating a pass only on active
boards at the present. Inactive boards and boards ahead of the present are not
advanced for this check. PGN check symbols use the same rule from the opponent's
perspective after each move; during an unfinished action, the opponent's present
is the turn following the apparent present if that still belongs to the mover.

## Castling in custom positions

Castling works on smaller boards and with rooks away from the corners. Mark
the king and participating rook as unmoved in the starting FEN (`K*` and `R*`
for White, `k*` and `r*` for Black). They must share a rank, with file coordinates
that differ by at least two and no pieces between them. Neither
piece needs to be on a back rank or an edge file.

The king moves two squares toward the rook, and the rook moves onto the square
the king crosses. If they start two files apart, the king finishes on the
rook's original square. For example, on a 4×4 board, a king on b1 and rook on
d1 finish on d1 and c1. Adjacent kings and rooks cannot castle together.
The king cannot castle out of, through, or into a physical attack, and the
completed action must satisfy the usual multiverse check restrictions.

## Supporting utilities

Convert a 5DUCI position command to full 5DPGN:

```sh
python scripts/uci2pgn.py 'position startpos moves (0T1)e2e3 submit'
```

Open [the line comparator](../../tools/compare.html) in a browser to compare pasted text.

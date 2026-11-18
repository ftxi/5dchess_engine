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
-  `perftest [<policy>]`: on each intermediate state, print 1 if an action is available, 0 otherwise
-  `hc-profile [options]`: profile final positions and main-line searches with internal C++ timing
-  `rollout [options]`: run and report random rollout simulations
-  `replay-log <log> [seed]`: replay and time a protocol failure log

Search policies are `deferred` (default), `balanced`, `naive`, `stable`,
`iterative`, and `fine`. The `fine` policy creates a
fine tree rooted at the selected position.
For `checkmate` and `perftest`, it searches for one witness action; `count` and
`all` traverse complete action branches up to the requested limit. Printed
actions use the same PGN format as the other policies. The explored tree is
retained during the search, so enumeration can require more memory.

The `deferred` policy retains invalid slices as negative constraints. It first
tries matching in the stored HC and prunes unsupported arrivals there. If the
candidate avoids all retained slices, it proceeds directly to validation;
otherwise it uses temporary regions to avoid those slices before retrying
matching.

Deferred also reuses the last successful matching across HCs and failed
attempts, validating its edges against each new graph before repairing any
uncovered requirements. This can change action enumeration order. Other HC
policies start each point selection with an empty matching.

Slices of global codimension at most one are removed immediately. When retained
slices outnumber stored HCs, one slice of smallest global codimension is removed
from every HC. Equal counts retain the slices. Retained slices are also removed
permanently from each visited HC when their relative codimension there is at
most one. These cuts do not increase the HC count and repeat until no further
such cuts apply, including after unsupported-arrival pruning. Successful actions
are removed as usual. The C++ API uses `info.search<Ordering, hc_policy::deferred>(space, order)`;
omitting template arguments also selects Deferred, with ordering deduced from
the supplied object or natural ordering when omitted. Other HC policies use
`hc_policy::iterative`, `hc_policy::stable`, or `hc_policy::balanced`.

For `balanced`, `stable`, `deferred`, and `iterative`, search starts with physical
moves on every mandatory timeline, leaving other timelines untouched. It then
continues with the remaining choices. Continued enumeration groups similar
actions before returning to other jump choices. Physical moves
can vary within a group; jumps can vary their piece and arrival square while
keeping the same source and destination timeline-turns and the same timeline
pairing. Departures change together with their arrivals.
The complete legal-action set is unchanged, but enumeration order can differ.

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

## HC search profiling

Use a Release build for performance comparisons. Read workloads from stdin,
one record per line:

```sh
build/5dtools hc-profile --warmup-rounds 0 --rounds 1 <<'INPUT'
EXPECTED=20 COUNT [Board "Standard"]
PERFTEST [Board "Standard"] 1. Nf3 / Nf6
CHECKMATE [Board "Standard"] 1. Nf3
INPUT
```

Records start with `EXPECTED<… COUNT`, `EXPECTED=… COUNT`,
`EXPECTED>… COUNT`, `PERFTEST`, or `CHECKMATE`, followed by single-line 5DPGN.
Replace whitespace in multiline games (including comments) with spaces
before sending them. Empty lines are ignored. Games are parsed as records arrive;
search starts after EOF. Stdin is the only workload input; no input-selection
flag is needed.
Stdin cases use `stdin:<line number>` as their report's `file` identifier.

Every count record requires a comparison; plain `COUNT` and the old
`EXPECTED <n>` syntax are rejected. Counts apply to the final position:

| Example | Action limit | Pass condition |
| --- | --- | --- |
| `EXPECTED<100 COUNT …` | 100 | Exhausted with fewer than 100 actions |
| `EXPECTED=100 COUNT …` | 101 | Exhausted with exactly 100 actions |
| `EXPECTED>100 COUNT …` | 101 | Found 101 actions |

Comparisons are checked after every enumeration, including warmups, outside the
timer. A failed comparison records the observed count and returns exit code 1.
Thresholds are nonnegative decimal integers; `<0` is rejected. The resulting
action limit must be in 1..2,147,483,647. Malformed records and empty workloads
return 2; invalid PGN is a preparation failure returning 1. Count limits bound
actions found, not elapsed time; use the launcher's `--timeout` to bound the
whole invocation without an external timeout command.

`CHECKMATE` measures each game's final position. `PERFTEST` measures the
initial position and every submitted position on the last-variation main line,
matching the existing perftest convention. `COUNT` applies the requested bounded
comparison at the final position unless `AT` selects an earlier turn.
The former `--stdin`, `--checkmate`,
`--perftest`, and `--count` options are not accepted.
The original fixtures under `test/pgn/count/` have expected action counts of
30,134 and 55,332. The final `NP.5dpgn` position has eight actions. The custom
`four-timeline-knights.5dpgn` fixture adds a position with several variable axes
and 35,204 actions, so enumeration comparisons cover more than the original two cases.
Policies are `balanced`, `stable`, `deferred`, `iterative`, `fine`, and `naive`.

Games are parsed once. Warmups and measured rounds run in the same process;
warmup samples are discarded. Each timer covers finding a legal action or
proving exhaustion, including search-context setup/cleanup and terminal
classification. Parsing, replay, action validation, and printing are excluded.
This does not run the separate softmate searches in `get_mate_type()`.
The engine uses softmate for check with legal escapes that all move the Present
backward, including through reactivation of existing timelines. Softmate is not
checkmate: legal play can continue.
For count records, the timer covers enumeration up to exhaustion or the action
limit, and context setup/cleanup; terminal classification is included when the count is zero. Count workloads
check the observed count for agreement between rounds, rather than validating
each individual generated action. Their samples measure bounded enumerations, not
individual actions.

JSON includes individual samples, per-game round totals, and mean, median, p95
(nearest rank), and maximum times in milliseconds. The perftest mean weights
each position sample equally. Preparation, replay, invalid-action, and
round-consistency failures are recorded explicitly; incomplete results return
exit code 1. Invalid command options return 2.
Count cases and their measured samples include an integer `action_count`.
An optional `AT=<turn>` before `COUNT` selects the position after that submitted
main-line turn, using the same selectors as `count --at`: `0` for the initial
position, or a turn such as `11b`. For example, `EXPECTED=602 AT=1b COUNT` followed
by the contents of `test/pgn/small.5dpgn` profiles its position after Black's first
action. Omission selects the final position. Unavailable turns are preparation
errors; selected cases and their prefixes include the normalized `at` in JSON.
Replay and selection occur before timing. Later actions are not replayed or
validated, although the complete PGN must still parse. `AT` requires `COUNT`.

The count is the number of actions observed; `exhausted: true` establishes an
exact total. At the action limit, `exhausted` is false even if the actual total
happens to equal that limit. Cases include `expected_count`, `expected_operator`,
and `action_limit`. The `action_count` and `exhausted` case fields are `null` if
no enumeration completed. Count-workload timing statistics appear under
`summary["profile-count"]`. Bounded cases may complete successfully without
exhausting the search space.

Use `--prefixes 10,100,1000` to add a separate enumeration at each requested
action limit before each original `COUNT` case. The original comparison still
runs, so `EXPECTED=30134` verifies complete enumeration independently of the
prefixes. Prefix cases use `workload: "profile-prefix"` and include
`requested_actions`, `action_limit`, `action_count`, and `exhausted`. Their timing
summary is separate from `profile-count`; compare matching requested limits
between policies. If a position has only eight actions, a request for ten reports
eight with `exhausted: true`. Reaching the requested limit does not prove exhaustion.

The launcher's default manifest requests prefixes 10, 100, and 1,000 through
`"count_prefixes": [10, 100, 1000]`. Override them with `--prefixes 100,1000`, or
use `--prefixes none` for the original comparison alone. Counts must be distinct
integers in 1..2,147,483,647. These options do not change checkmate or perftest cases.

For allocation and live-HC measurements, enable the optional instrumentation:

```sh
cmake -S . -B build-hc-profile -DTOOLS=ON -DTEST=ON -DPYMODULE=OFF \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-fno-rtti -DHC_PROFILE_METRICS=ON
cmake --build build-hc-profile --config Release --parallel
python scripts/profile_hc.py --executable build-hc-profile/5dtools \
  --policy deferred --workload count --metrics
```

The native command also accepts `--metrics`. Each measured search is followed
by a separate diagnostic search at the same action limit. Only the first search
contributes `elapsed_ms`; the second supplies `allocations` and `peak_live_hcs`.
Its action count or availability must agree with the timed search. Parsing,
replay, validation, and report generation are outside the diagnostic scope.

`allocations` counts successful C++ `new`/`new[]` calls on the search thread,
including aligned allocations and search-context setup/cleanup. It does not count
direct `malloc` calls or allocations on other threads. `peak_live_hcs` counts
actual HC objects created during that search, including context universes,
temporary regions, copies, empty HCs, and moved-from objects until destruction.
Implicit future pieces are excluded until an HC is constructed. This is an object
count, not heap bytes or process memory.

JSON samples carry the two integer counters. Each case's `metrics` object contains
their mean, median, p95, and maximum, with `null` values when no measurements
completed. Text output includes median allocations and maximum live HCs. JSON
also reports `metrics_enabled` and `metrics_available`. The manifest can set
`"metrics": true`; `--no-metrics` disables it in the Python runner.

Instrumentation is off in normal builds. When enabled it adds HC lifetime tickets
and allocation hooks, even though diagnostic counting is active only during the
separate pass. Compare policies built with the same instrumentation settings;
do not interpret differences between instrumented and normal builds as policy
speedups. Requesting metrics from a build without instrumentation returns an error.

Press Ctrl+C to print a partial report immediately and exit with code 130.
Only completed, validated measured searches are included; the unfinished search
is omitted. Cases and workload summaries indicate whether all requested rounds
finished. JSON also records `interrupted: true`. The launcher saves this partial
report to its usual output path and prints the available timing summaries.

For a fixed workload, use [the launcher](../../scripts/profile_hc.py) and
[its manifest](../../scripts/profile_hc_games.json):

```sh
python scripts/profile_hc.py
python scripts/profile_hc.py --policy stable --workload checkmate --rounds 3
python scripts/profile_hc.py --policy deferred
python scripts/profile_hc.py --policy deferred --workload count
python scripts/profile_hc.py --config scripts/profile_hc_expanded.json
```

Count entries in `profile_hc_games.json` require a file and comparison:

```json
"profile-count": [
  {"file": "test/pgn/NP.5dpgn", "expected": "=8"},
  {"file": "test/pgn/small.5dpgn", "at": "1b", "expected": "=602"}
]
```

Use `"<1000"` for an upper bound or `">100"` for a lower bound. Checkmate and
perftest entries remain filename strings.
The optional string `at` selects a submitted turn; omission selects the endpoint.
Several count entries may reference the same file at different turns. Duplicate
file/turn pairs are rejected. Reports retain the file and selected turn.

The default manifest records `build/5dtools`, four gigantic checkmate fixtures,
50 fixed online games, 20 representative count positions, and the default rounds.
The [expanded count manifest](../../scripts/profile_hc_expanded.json) contains
the other 255 count positions. Both reference existing games with optional turn
selectors, fully enumerate known spaces up to 200,000 actions, and cap larger
spaces at 50,001 using `>50000`. The
[corpus guide](../../test/pgn/count/README.md) describes selection and provenance.
Paths in the manifest are relative to
the repository root; the launcher works from any directory. It reads and flattens
each game into a stdin record, so filenames do not enlarge the native command
line. It restores source filenames in the report using stdin record numbers.
It invokes C++ once and saves JSON under `logs/hc-profiles/`, including the effective command,
manifest, and executable/input hashes. It does not resample games or build the
executable. Use `--executable` for another build and `--output` for another
result path. `--timeout <seconds>` bounds the entire native invocation, not
individual searches; an exceeded limit saves an explicit timeout result and
returns 124.

The [count regression script](../../test/hc_count.sh) embeds the 30,134, 55,332, and 1,494
cases and runs without separately linked test executables:

```sh
sh test/hc_count.sh build/5dtools --policy deferred
```

With `TOOLS=ON` and `TEST=ON`, CTest checks these records with Balanced, Stable,
Deferred, and Fine. CTest feeds the shell script's literal
records through CMake, so these count checks do not require a shell.

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

# Test suite of Python scripts

Run from the repository root with Python 3.11 or newer (3.12 is tested):

```sh
python -m unittest discover -s scripts/test -v
```

From `scripts/`, use `python -m unittest discover -s test -v`.

## Tested files

`test_uci2pgn.py` checks the converter CLI with adjacent and separated FEN
blocks, signed timeline labels, a complete two-timeline history, even-timeline
jumps and promotions, odd timelines, and invalid text between FEN blocks.

`test_matchmaker_options.py` checks common job defaults and aliases, named CPU
pool coverage, affinity fallback, and prevention of repeated explicit engine
seeds.

`test_engine_process.py` checks missing executables before CPU-affinity launch,
both startup handshake barriers, startup exits and timeouts, rejection of
non-engine programs, and preservation of early stderr errors before long
usage messages. These checks use small Python subprocesses and need no native
build.

`test_ratio_matchmaker.py` checks provisional descent, refinement after a
setback, retained evidence, superiority mode, budget exhaustion, the shared
noninferiority boundary, continuous worker refill, ordered pair commitment,
and cleanup on cancellation or a formal stop. Its scheduling checks control
completion with events rather than timing-dependent performance assertions.
Output checks cover separate game and pair lines, five-second throttling,
combined pair updates without repeated game totals, buffered and void counts,
immediate decisions, fixed-look p values, and concise final summaries.
Calibration checks verify the Standard startpos, configured search time, and
fixed mean of actual target iteration counts across worker CPUs.

`test_profile_hc.py` checks required count comparisons in the profiling manifest,
their stdin encoding, filename mapping in saved reports, and rejection of invalid
or overflowing limits. It mocks the native process; native enumeration counts
are checked by the `hc_count_*` CTest checks.

`test_public_api.py` checks these files through their public interfaces:

| File | What passing verifies |
| --- | --- |
| [`scripts/elo.py`](../elo.py) | Registry edits, enable/disable, scheduling, reporting, ratings, history, and cancellation through CLI output. |
| [`scripts/autoplay.py`](../autoplay.py) | Legal games, PGN metadata, metrics, history sent to engines, path handling, shutdown, and diagnostics for illegal moves, missing moves, exits, errors, and timeouts. |
| [`scripts/elo_matchmaker.py`](../elo_matchmaker.py) | Concurrent queued games, resume, saved results/files, capped games recorded as draws, and recovery after disabling a failed engine. |
| [`scripts/ratio_matchmaker.py`](../ratio_matchmaker.py) | Both statistical modes, color reversal, distinct process seeds, outgoing iteration budgets, baseline CLI names, explanatory header, actual child CPU masks where supported, logs, summaries for known outcomes, and actionable startup errors. |
| [`scripts/host.py`](../host.py) | HTTP HTML response and socket messages for loading, moving, undo/redo, submission, navigation, cancellation, PGN export/reload, and hints. |
| [`pymodule.cpp`](../../pymodule.cpp), through compiled `engine` | Coordinate/move objects, legal and illegal moves, undo/redo, submission, history navigation, comments, and PGN round trips. |

The binding tests cover `game.get_current_hash()` across metadata changes
and history navigation. Child-operation checks cover membership, insertion without navigation, duplicate rejection, existing-only navigation, comments, deletion, and last-occurrence replacement of duplicate imported branches. This hash uses `hash_state`; equal hashes require an
exact endpoint comparison before removing duplicate fixtures.

Controllers use scripted 5DUCI engine subprocesses with fixed moves/statistics
and deliberate faults. The actual controllers and compiled rules run normally.
A separate smoke check uses the real `5dchess zero` engine. Temporary databases,
logs, and game files are removed afterward.

## Positions and short games

The standard fixture is the 8×8 opening with White to move:

```text
[Board "Standard - Turn Zero"]
```

The host also loads this 4×4 custom position and requests a navigable hint:

```text
[Size "4x4"]
[Board "Custom"]
[Mode "5D"]
[nbrk/3p*/P*3/KRBN:0:1:w]
```

- Binding and host move/history checks play `(0T1)d2d3` from the standard opening.
- Scripted controller games play `(0T1)d2d3`, then `(0T1)d7d6`, capped at two actions. Autoplay and matchmaker request 30 ms per move. Fault cases may end earlier; the illegal move is `(0T1)d2d8`.
- Matchmaker runs two games with two workers, then resumes one separately scheduled game. Its failure-recovery case queues two games with one worker.
- Ratio CLI checks run one color-reversed pair per scenario at ratio `0.5` and two actions per game, with unique automatic seeds for engine processes. Baseline receives 40 ms; target time is capped at 200 ms. Fixed reference statistics of 100 iterations produce a target budget of 50 iterations.
- The real-engine smoke game starts from the standard opening, requests 40 ms per move, and stops after two actions. Its exact moves and timing are not asserted.

Passing verifies these workflows, not exhaustive chess rules, playing strength,
statistical validity, browser rendering, or live network transport. Host checks
use Flask and Socket.IO test clients.

## If tests are skipped

Skipped tests indicate missing dependencies, not verified behavior. Run the
relevant commands below from the repository root, then rerun the suite:


```sh
# Missing a submodule:
git submodule update --init --recursive

# Missing Flask / Flask-SocketIO:
python -m pip install flask flask-socketio

# Missing compiled binding or engine (POSIX shell):
cmake -S . -B build -DPYMODULE=ON -DENGINE=ON -DCMAKE_BUILD_TYPE=Release \
  -DPython_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')"
cmake --build build --parallel
```

The host checks skip when `extern/client/templates/index.html` is absent;
other tests still run. To fetch only the host's client, append `extern/client`
to the command above. The `extern/pybind11` submodule is needed to build the
Python binding.

Use the same Python for installation, compilation, and tests. An existing
binding that cannot load fails rather than skips; rebuild for the selected
interpreter. See the [build guide](../../docs/usage/building.md#python-module) for
build prerequisites and interpreter selection. Full validation requires a
passing run with zero skips.

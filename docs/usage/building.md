# Building

Run the build commands below from the repository root unless stated otherwise.

## Requirements

- CMake 3.20 or newer.
- A compiler and standard library with C++23 support.
- Python 3.11 or newer for Python bindings and scripts.
- Emscripten for WebAssembly builds.

The release workflow builds with GCC 13 on Linux, Apple Clang with Xcode 26 on macOS, and Visual Studio 2022 on Windows. Older toolchains may lack required standard-library features such as `std::jthread` and `std::stop_token`.

## Native programs

```sh
cmake -S . -B build -DENGINE=ON -DTOOLS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Use Release builds for search performance. Compiler optimizations have a substantial effect on this project.

Single-configuration generators put the executables at `build/5dchess` and `build/5dtools`. Multi-configuration generators such as Visual Studio put them under `build/Release/`; Windows executables have an `.exe` suffix. Adjust executable paths in examples accordingly.

### Build options

| Option | Product |
|---|---|
| `ENGINE` | 5DUCI engine executable |
| `TOOLS` | Command-line utilities |
| `TEST` | Native tests |
| `JSTEST` | Copy JavaScript tests into the WebAssembly interface |
| `PYMODULE` | Python binding |
| `EMMODULE` | WebAssembly binding and static web interface |

When `TOOLS` or `TEST` is enabled, PGN fixtures are copied from `test/pgn/` to the build directory’s `pgn/` folder, including subfolders.

All options default to off. With every option disabled, CMake builds only the core C++ library. Enable both `TOOLS` and `TEST` to include tools-related tests. See [Contributing](../../CONTRIBUTING.md#testing) for test commands.

Native regression test executables are grouped by functionality:

| Executable | Coverage |
|---|---|
| `test_action` | Actions, semimoves, move application, and promotions |
| `test_movegen` | Pawn and brawn movement, captures, en passant, promotion, and castling on small boards and with interior rooks, including move application and physical attack restrictions |
| `test_game` | Multiverse ownership and layout, indexed game trees, child operations, hashing, temporary move previews, submission policy, and illegal history |
| `test_check_position` | Check views and phantom checks |
| `test_hcutils` | Hypercuboid geometry and graph matching |
| `test_move_evaluation` | Move scoring and HC semimove features |
| `test_mcts` | MCTS infrastructure, policies, and linear engine variants |
| `test_misc` | Integer sets, generators, and ordering utilities |
| `test_bitboards` | Bitboards and attack tables |
| `test_finetree` | Fine-tree expansion and navigation |
| `test_statistics` | Core position statistics |
| `uci_scriptted` | Protocol options, positions, error recovery, task completion, and readiness |

Each grouped executable prints the subgroup name before running it. For example,
`ctest --test-dir build -R '^test_mcts$' --output-on-failure` runs the MCTS and
linear engine tests. Executables from files without the `test_` prefix retain
their individual names. Reconfigure CMake after adding, removing, or renaming
native test source files so its source glob and CTest registrations are refreshed.

### Building without RTTI

The native libraries, engine, and tools support builds with RTTI disabled. For
GCC or Clang, configure with `-DCMAKE_CXX_FLAGS=-fno-rtti`, for example:

```sh
cmake -S . -B build-no-rtti -DENGINE=ON -DTOOLS=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS=-fno-rtti
cmake --build build-no-rtti --config Release --parallel
```

For MSVC, use `-DCMAKE_CXX_FLAGS=/GR-` instead. Keep `PYMODULE` disabled for these
builds because pybind11 requires RTTI. Option debug messages use the readable
type names `monostate`, `bool`, `int`, `double`, and `string`.

## IDE setup

Generate an Xcode project:

```sh
cmake -S . -B build-xcode -DTOOLS=ON -DTEST=ON -G Xcode
```

Or a Visual Studio 2022 project:

```cmd
cmake -S . -B build-vs -DTOOLS=ON -DTEST=ON -G "Visual Studio 17 2022"
```

## Python module

Initialize the submodules in an existing checkout:

```sh
git submodule update --init --recursive
```

For a fresh checkout:

```sh
git clone --recurse-submodules https://github.com/ftxi/5dchess_engine.git
cd 5dchess_engine
```

The `extern/pybind11` submodule is needed for the binding; `extern/client` provides the graphics interface used by the Python host.

For the [graphics interface](https://github.com/SuZero-5DChess/5dchess_client)
and web API tests, install Flask and Flask-SocketIO in that interpreter:

```sh
python -m pip install flask flask-socketio
```

From the repository root, configure and build using your selected Python
(the following example uses a POSIX shell):

```sh
cmake -S . -B build -DPYMODULE=ON -DENGINE=ON -DCMAKE_BUILD_TYPE=Release \
  -DPython_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')"
cmake --build build --config Release --parallel
```

Optionally, run the Python tests:

```
python -m unittest discover -s scripts/test -v
```

To use the graphical interface with python backend, go to the base directory of this project and run `python scripts/host.py`. Then, visit `http://127.0.0.1:5000` with your favourite browser.

When using the Python game API, `game.can_submit()` and `game.submit()` reject
turns that leave a friendly royal exposed. To inspect such history explicitly,
pass `allow_submit_with_checks=True` to both methods. This still requires a
completed turn and marks accepted exposed actions as illegal history. Loading
such games separately requires `game.from_pgn(pgn, allow_illegal_history=True)`.

## WebAssembly

Requires [emscripten](https://emscripten.org).

```sh
emcmake cmake -S . -B build-wasm -DEMMODULE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --parallel
```

The static website is generated in the `build-wasm/ui/`.

UI tests are copied into `ui/test/` only when `TEST` or `JSTEST` is enabled.
Use `-DJSTEST=ON` to include them without building native tests. Disabling both
options skips copying tests but leaves any previously copied tests in place.

On opening the interface, the engine initializes alongside chess-piece loading.
The starting position appears once both are ready; bundled board-label fonts
load in the background.

When zoomed out, board surfaces and frames use cached images; highlighted
boards keep direct drawing to preserve their highlight layering. Zooming in
restores direct drawing and the file/rank labels. Phantom-board labels use the
same opacity as their board, combined with their zoom fade.

The analyzer prepares board images while checking an import. The import dialog
shows **Checking import and preparing boards…** while both tasks run, then
**Checking import…** or **Preparing boards…** if one finishes first. The loaded
position opens after both tasks finish and the import is accepted. Zoomed-out
boards use these images even at the furthest zoom; close views show file/rank
labels. Phantom-board opacity applies uniformly to each board
surface, including its labels.

For interface structure and behavior, see
[Web UI architecture](../architecture/webui.md).

### Serving the browser interface

On opening the analyzer, a text-only loading screen shows numbered checkpoints
while WebAssembly and chess pieces load, the interface is prepared, and the
starting position loads. The final checkpoint includes preparing board images;
the loading screen closes only after preparation finishes and the position is
drawn. Checkpoint 3 names whichever resource is still loading
and may be skipped. The numbers identify startup checkpoints rather than a
percentage. The screen closes when the starting board is drawn.

Note that simply double-clicking index.html will likely fail to initialize the JavaScript components due to [CORS (Cross-Origin Resource Sharing)](https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/CORS) restrictions enforced by modern browsers when using the `file://` protocol.

To run the application correctly, you must serve the directory via a local web server. Use one of the following methods after running `cd build-wasm`:

If you have python installed:
```sh
python -m http.server 8080 --directory ui/
```
If the emsdk is already sourced in your environment:
```sh
emrun ui/
```
If you prefer [darkhttpd](https://github.com/emikulic/darkhttpd):
```sh
darkhttpd ui/
```

### Correcting PGN imports

Long game histories and deeply nested variations can be imported in browser
workers, including Safari. Importing and discarding these trees no longer use
call-stack space proportional to their depth; available memory still limits
the size of a game.

If a PGN cannot be loaded, the import dialog stays open and shows the full error
below the input. Hover over the underlined yellow location to highlight the
problem; click or tap it to pin or unpin the highlight. Keyboard focus also
previews it. Locations use an exclusive end, and missing input has a caret marker.
Invalid Board/Variant, Size, Timeline, and Promotions values point to the relevant
header value. A missing initial setup has a message without a location.

Editing removes the highlight but keeps the message marked **From previous
load**. Its old location is disabled until the next Load attempt. Loading valid
PGN closes the dialog; a failed load leaves the current game intact. Errors about
exposed royal pieces explain how to enable **Allow submit with checks** in
Settings when you want to inspect illegal history.

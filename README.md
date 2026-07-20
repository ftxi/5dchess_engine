5dchess_engine
==================


The `5dchess_engine` is a standalone program that can also be used as a library for analyzing 5D chess game. Written in c++, it is also compiled for use in python and javascript environments. When used as a standalone tool, it offers both a command line interface and a web-based interface for viewing and analyzing games.

This project is written in a serious language for chess-programming (c++). It aims to provide fast performance for basic game logic such as move generation and checkmate detection, which can be used as a solic foundation for a competant 5d chess bot. 

There is a 5d chess bot implemented in this project, which utilizes a customized Monte Carlo Tree Search algorithm. Plans for the near future is to try out different modifications of MCTS to improve the performance of the bot.

### Try it online!

Visit <https://ftxi.github.io/5dchess_engine/>.

### Features


This program supports reading arbitary 5d chess variant specified by 5dfen. For moves, it supports long algebraic notation (which looks like `(0T13)b6b5` for physical moves and `(-1T19)e8(0T18)f8` for superphysical moves) or simplified 5dpgn notation specified in [docs/pgn-bnf.txt](docs/pgn-bnf.txt).

The storage of a game state is based on [bitboards](https://www.chessprogramming.org/Bitboards). As a result, all boards are hard-coded to be no larger than `8x8`.

Currently, the engine implements move generation and check detection using coroutine-based generators. Thus it won't work on compilers pre-C++20.

For checkmate detection and action generation, this program implements the hypercuboid algorithm. The hypercuboid algorithm is also utilized in `core/fine_tree.h` for generating semimoves. This ensures that the branching factor of the search tree does not explode exponentially with the number of timelines.

This program supports tree shaped traversal.

### Usage

The CMake program and a modern C++ complier (C++ 20 or newer) is required. On MacOS, Xcode is enough. On windows, I suggest Visual Studio Community version 2022.

There are a number of ways to use the program:
1. Use the static webpage hosted on github pages. See *Try it online* above.
2. Use command-line interface. No dependencies other than cmake and a c++ compiler. See *Build Test*.
3. Build python module and host a graphics interface server via python. Requires a python runtime with `flask` and `flask_socketio` installed. See *Build Python Module*.
4. Build javascript module and host the static webpage same as the online version. See *Build WASM*.

#### Build Tests

```sh
mkdir build
cd build
cmake .. -DTEST=on -DCMAKE_BUILD_TYPE=Release
cmake --build .
```
The performance of this code depends significantly on compiler optimizations. Without optimization, the plain (unoptimized) version may run x6 ~ x7 times slower compared to the same code compiled with `-O3` optimization.
The flag `-DCMAKE_BUILD_TYPE=Release` above is used to enable optimizations.


The command line tool will be built as `build/cli`. To use it, type `cli <option>`, press enter, and then input the game in 5dpgn (press control+D to complete). Current features of the command line tool including:
-  `print`: print the final state of the game
-  `count [fast|naive] [<max>]`: display number of avialible moves capped by <max>
-  `all [fast|naive] [<max>]`: display all legal moves capped by `<max>`
-  `checkmate [fast|naive]`: determine whether the final state is checkmate/stalemate
-  `diff`: compare the output of two algorithms.
-  `perftest [fast|naive]`: on each intermediate state, print 1 if it is checkmate/stalemate, 0 otherwise
-  `uci`: enter Universal 5D Chess Interface mode and work as a chess engine

#### Engines and autoplay

There are two existing engines: `cli uci mcts` and `cli uci monkey`; they communicate using the [5DUCI protocol](docs/5duci.md). To create an engine, derive the `engine` class in `src/engine/uci.h`. You must implement `initialize()` and `find_best_move()`, then start its `mainloop()` with an `io_handler`.

To play a match between two engines, first build the Python module (run `cmake` with `-DPYMODULE=on`), then run `autoplay.py` with the two engines specified as arguments. Example:
```sh
python autoplay.py --white "./build/cli uci mcts" --black "./build/cli uci monkey"
```
Use `--help` for more information on how to set a starting game, time controls, or a multi-game series.

#### Coding with IDE

It is possible to run the c++ part of the code without interacting with python or web interface at all. It also makes sense to use a modern programming IDE:
```sh
mkdir build-xcode
cd build-xcode
cmake .. -DTEST=on -GXcode
```
On Windows, the last line should be:
```cmd
cmake .. -DTEST=on -G"Visual Studio 17 2022"
```

### Build Python Module

<bold style="color:#ff6347;">**IMPORTANT NOTE**</bold> This module rely on two separate submodules. It is impossible to build the python library without them. Make sure use
```sh
git clone --recurse-submodules <link-to-this-repo>
```
to download both this repository and the necessary submodules.

If interaction with the [graphics interface](https://github.com/SuZero-5DChess/5dchess_client) is preferred, please install `flask` and `flask_socketio` via `pip`.

```sh
mkdir build
cd build
cmake .. -DPYMODULE=on -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

To use it, go to the base directory of this project and run `host.py`. Then, visit `http://127.0.0.1:5000` with your favourite browser.

### Build WASM

Requires [emscripten](https://emscripten.org).

```sh
mkdir build-wasm
cd build-wasm
emcmake cmake .. -DEMMODULE=on -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

The static website is generated in the `/build-wasm/ui/`. 
Note that simply double-clicking index.html will likely fail to initialize the JavaScript components due to [CORS (Cross-Origin Resource Sharing)](https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/CORS) restrictions enforced by modern browsers when using the `file://` protocol.

To run the application correctly, you must serve the directory via a local web server. Use one of the following methods from within the `build-wasm/` folder:

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

### Disclaimer

All resources inside this project are either open source online or created by myself. It does not use any source code, copied directly or decompiled, from the 5D Chess With Multiverse Time Travel by Thunkspace, LLC. The original game is a commercial product and I have no affiliation with the developer.

### Documentation

For more details on the structure of this repository, please read [this page](docs/index.md).

### TODOs
- [x] Write standard of and implement 5duci for communication.
- [x] Split `src/` folder into client-specific and engine-specific folders.
- [ ] Modify cmake file to support building core only/build engine.
- [x] Create a basic 5d chess bot.
- [ ] Write take_random_point() for hypercuboid algorithm and use it for rollout in MCTS default policy. In rollout, picking which fine cell to continue might be tricky to be made uniform.
- [x] Figure out the reason for unexpected nobestmove.

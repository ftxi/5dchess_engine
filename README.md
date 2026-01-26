5dchess_engine
==================

<bold style="color:#ff6347;">**IMPORTANT NOTE**</bold> This module rely on two separate submodules. It is impossible to build the python library without them. Make sure use
```sh
git clone --recurse-submodules <link-to-this-repo>
```
to download both this repository and the necessary submodules.

If interaction with the [graphics interface](https://github.com/SuZero-5DChess/5dchess_client) is preferred, please install `flask` and `flask_socketio` via `pip`.

### Features

5dchess_engine is an engine for analysing 5D chess game. It can be used as a C++ library or a python library. There are two ways to present a game: via cli (no dependencies) or webpage (requires python, read "IMPORTANT NOTE" above).

This program supports reading arbitary 5d chess variant specified by 5dfen. For moves, it supports long algebraic notation (which looks like `(0T13)b6b5` for physical moves and `(-1T19)e8(0T18)f8` for superphysical moves) or simplified 5dpgn notation specified in [docs/pgn-bnf.txt](docs/pgn-bnf.txt).

The storage of a game state is based on [bitboards](https://www.chessprogramming.org/Bitboards). As a result, all boards are hard-coded to be no larger than `8x8`.

Currently, the engine implements move generation and check detection using coroutine-based generators. Thus it won't work on compilers pre-C++20.

There are two checkmate detection program: 
1. hc, using method from [here](https://github.com/penteract/cwmtt), adapted to c++ with improvements.
2. naive, plain DFS searching pruning states with checks/moves not in order.

From my testing, hc has a better worse case performance than naive, especially when the search space is large while available actions are sparse, e.g. when the situation is almost checkmate. However, naive usually perform better when options are abundant.

This program supports tree shaped traversal.

### Usage (MacOS, etc.)


```sh
mkdir build
cd build
cmake ..
make
```

After that, *go to base directory* and run `host.py`. If the server starts smoothly, the chessboard can be found at <http://127.0.0.1:5000>.

### Usage (Windows)

The CMake program and a modern C++ complier (C++ 20 or newer) is required. I suggest Visual Studio Community version 2022.

```cmd
mkdir build
cd build
cmake ..
cmake --build .
```

The last step is same as above.

### Debugging/Command Line Interface
It is possible to run the c++ part of the code without interacting with python or web interface at all. It also makes sense to use a modern programming IDE:
```sh
mkdir build_xcode
cd build_xcode
cmake .. -DTEST=on -GXcode
```
On Windows, the last line should be:
```cmd
cmake .. -DTEST=on -G"Visual Studio 17 2022"
```

The performance of this code depends significantly on compiler optimizations. Without optimization, the plain (unoptimized) version may run x6 ~ x7 times slower compared to the same code compiled with `-O3` optimization.

To enable optimizations, configure the build using:

```
cmake .. -DCMAKE_BUILD_TYPE=Release -DTEST=on
```

The command line tool will be built as `build/cli`. To use it, type `cli <option>`, press enter, and then input the game in 5dpgn (press control+D to complete). Current features of the command line tool including:
-  `print`: print the final state of the game
-  `count [fast|naive] [<max>]`: display number of avialible moves capped by <max>
-  `all [fast|naive] [<max>]`: display all legal moves capped by `<max>`
-  `checkmate [fast|naive]`: determine whether the final state is checkmate/stalemate
-  `diff`: compare the output of two algorithms



### Documentation

For more detail, please read [this page](docs/index.md).

### TODOs
- [x] checkmate display
- [x] merge pixels
- [ ] ~~flask static path~~
- [x] embind
- [x] debug nonstandard pieces
- [ ] editing comments
- [ ] L/T numbers
- [ ] documentation
- [x] variants loading


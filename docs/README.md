# Using and understanding the project

This project combines a 5D Chess game logic library, search engines, and interfaces for analyzing games. You can use the analyzer directly, run engines and experiments locally, or build another application on top of the same game logic. Boards can be up to 8 × 8, and games can contain branching timelines and variations.

## Analyze a game

The quickest way to start is the [online analyzer](https://ftxi.github.io/5dchess_engine/), which runs in your browser. It lets you explore positions and branching game histories without building the project.

For local use, there are two interfaces: the static WebAssembly analyzer in `ui/`, and a Python-hosted interface supplied by the `extern/client` submodule. The [build guide](usage/building.md) covers both, along with the native programs and Python binding. [Web UI architecture](architecture/webui.md) describes the static interface’s files, worker communication, rendering, and component behavior.

Games use [Branched 5DPGN](branched-5dpgn.md) to describe positions, moves, and variations. Physical moves stay on one board; superphysical moves travel between boards in time or across timelines. A turn may require several moves before it can be submitted. The notation guide explains these concepts and their representation; [the formal grammar](pgn-bnf.txt) specifies the parser syntax.

## Run engines and experiments

The native `5dchess` executable provides several search engines, including MCTS variants, linear evaluation variants, flat-UCB, and a random baseline. They accept positions and search requests through the text-based [5DUCI protocol](5duci.md). Start with [engine usage](engine/usage.md) to choose an engine and configure its search.

To compare engines, `scripts/autoplay.py` runs matches and records games and search metrics. The [engine usage guide](engine/usage.md#autoplay) explains match setup. For persistent ratings and automated opponent selection, use the [Elo registry and concurrent matchmaker](engine/elo-matchmaker.md).

For position-scaled iteration budgets, use the [adaptive iteration-ratio matcher](engine/iteration-ratio-match.md), which pins opponents to the same CPU and searches for a noninferior ratio.

Both matchmakers use the same [job, CPU, output, opening, and randomness options](engine/matchmaker-options.md).

The `5dtools` executable inspects positions, counts or lists legal actions, checks for checkmate, and runs rollout and algorithm comparisons. See [command-line tools](usage/tools.md) for commands and position selection. These tools are useful for investigating game logic independently of engine strength.

## Build on the game logic

The C++ core handles board states, timelines, move generation, legal actions, check detection, parsing and prinying. The Python and JavaScript bindings expose this logic for scripts and interfaces.

A game holds its current state and history. Each state contains a multiverse of timelines, whose boards can be shared between states to avoid copying unchanged data. A move changes a board or travels between boards; an action groups the moves that together complete a player's turn.

Generating legal actions becomes difficult as the number of timelines grows. The [hypercuboid algorithm](hypercuboid.md) searches combinations of moves while removing invalid regions of the search space. The [graph matching article](graph-matching.md) explains the related matching algorithm. Search engines use the resulting game logic to explore candidate actions; the linear engines also use a [bounded feature model](engine/linear-features.md) to evaluate positions.

## Repository structure

| Location | Purpose |
|---|---|
| [`src/core/`](../src/core/) | Boards, multiverses, actions, legality, and notation |
| [`src/engine/`](../src/engine/) | Search engines and 5DUCI communication |
| [`src/client/`](../src/client/) | Game history and navigation for interfaces |
| [`src/misc/`](../src/misc/) | Shared low-level utilities |
| [`pymodule.cpp`](../pymodule.cpp), [`emmodule.cpp`](../emmodule.cpp) | Python and WebAssembly bindings |
| [`ui/`](../ui/) | Static browser analyzer |
| [`scripts/`](../scripts/) | Python hosting, autoplay, ratings, and supporting scripts |
| [`tools/`](../tools/) | Native executable entry points and analysis utilities |
| [`test/`](../test/), [`scripts/test/`](../scripts/test/) | Native tests, game fixtures, and Python tests |
| [`extern/`](../extern/) | Third-party submodules, including the Python-hosted client |

For implementation details, read [Core game logic](architecture/core.md) and [Client architecture](architecture/client.md). IDE setup is covered in [Building](usage/building.md#ide-setup). An engine architecture article is planned once the code stabilizes, covering the templates and concepts used to assemble `TreePolicy` and `DefaultPolicy`. Before changing the project, read [Contributing](../CONTRIBUTING.md), which also gives test commands. The [Python test guide](../scripts/test/README.md) describes that suite's coverage and dependencies.

## Reports

Browse [experimental reports](reports/) for recorded experiments and results.
Report images, data tables, and measurement summaries are stored in
[`reports/res/`](reports/res/).

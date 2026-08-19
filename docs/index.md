Structure of the code
========

Engine-specific references:

- [Search metrics](metrics.md) defines the MCTS metric hierarchy, timing and
  workload counters, and complete-action root distribution fields.
- [Linear evaluation features](linear-features.md) defines the 84-feature
  layout, player perspective, and built-in heuristic weights.
- [Training the linear evaluation](linear-training.md) defines target
  perspective, optimization, checkpoint formats, and the training CLI.
- [5DUCI](5duci.md) defines the engine protocol.

The following image roughly captures the structure of the code:
```
  ┌──────────────┐                                             
  │ game         │                                 in stack    
  │     ╷  ╷ ... │                                             
  └─────┼──┼─────┘                                             
        │  └───────────────────┐                               
  ┌─────┴────────┐      ┌──────┴────────┐                      
  │ status 0     │      │ status 1      │  ...     in heap   
  │     ╷        │      │      ╷        │                      
  └─────┼────────┘      └──────┼────────┘                      
        │                      │                               
  ┌─────┴────────┐      ┌──────┴────────┐                      
  │ multiverse 0 │      │ multiverse 1  │                      
  │     ╷╷╷      │      │    ╷╷╷╷       │                      
  └─────┼┼┼──────┘      └────┼┼┼┼───────┘                      
        |||                  |||└──────────┐
        │││   ┌──────────────┘│└─────┐     |       in heap     
        │││   │      ┌────────┴────┐ │     │                   
        │└┼───┼──────┤board (1T1)w │ │     │           ...     
        │ └───┼────┐ └─────────────┘ │     │                   
   ┌────┴─────┴─┐  │ ┌─────────────┐ │┌────┴───────┐           
   │board (0T0)b│  └─┤board (0T1)w ├─┘│board (0T1)b│   ...     
   └────────────┘    └─────────────┘  └────────────┘           
                                                               
                                                               
```                                                         
I try to follow the terminalogy described in <https://github.com/adri326/5dchess-notation>. The classes defined are inspired from that. For now, a more comprehensive standard for notation is described in [pgn-bnf.txt](/docs/pgn-bnf.txt).

+ A `game` object (defined in `core/game.h`) contains the current state and some historical states. The collection of all relevent state is stored in a `gametree` object (defined in `core/gametree.h`). 

+ A `state` object (defined in `core/state.h`) contains a `multiverse` object and some additional information such as who is playing. The purpose for that is to distinguish between whether an action is being submitted or not.

+ A `multiverse` object (defined in `core/multiverse_base.h`) contains a 2-dimensional list of `board` pointers. Therefore, it is possible to reduce memory consumption by reusing board objects when the new multiverse is only partially different from the old one. There are two types of multiverses (defined in `multiverse_variants.h`): with odd/even initial timelines.

+ A `board` object (defined in `core/board.h`) contains 12 bitboards to describe pieces' arrangement inside. Thus its size is 12 bytes (96 bits).

In the code, there are two coordinate systems: LTCXY (which is the coordinate for storing moves) and UVXY (which is the coordinate for indexing boards). In both systems, X and Y ranges from 0 to 7. The difference is: L can be positive or negative while U must be greater than or equal to zero. TC are two axes but V is just one axis. The functions `l_to_u` `tc_to_v` `u_to_l` and `v_to_tc` (defined in `multiverse_base.cpp`) convert between these coordinates.

-----

Initializer of `vec4` object (defined in `misc/vec4.h`) follows the order `(x,y,t,l)`. It supports addition, substraction, scalar multiplication and and comparation. I will talk more about the implementation of this class below.

Movegen
========

For efficient move generation, this program uses bitboards (implemented in `core/bitboard.h`, etc.) and magic number hashing methods (implemented in `core/magic.h`). Note that magic numbers are only stored for rook/bishop movegen for classic chess. To adapt it for 5D Chess, I used copy masks (see `core/multiverse_base.cpp`).

Below was the python script used for generating code. It is no longer used though.
```python
from itertools import combinations, chain

def functions(domain, range):
    if len(domain) == 0:
        return [[]]
    else:
        x = domain.pop()
        fs = functions(domain, range)
        return [f+[(x,r)] for r in range for f in fs]

p = combinations(['x','y','t','l'], 4)
q = chain(*map(lambda x: functions(list(x), [1,-1]), p))

def show(f):
    for u in f:
        s = dict(u)
        for c in "xytl":
            s.setdefault(c, 0)
        print("vec4({x:2},{y:2},{t:2},{l:2})".format(**s), end=', ')

show(q)
```

Vector of four integers
=============

To implement `vec4` in a way that is very fast in addition, the program uses 
a trick from <https://stackoverflow.com/questions/79464417>. In `vec4.h`, it is
defined that the data of `vec4` is stored in a 32-bit integer, with
`X_BITS = Y_BITS = T_BITS = L_BITS = 8`, so valid values for each of them range
from -128 to 127. This means there can are maximally 256 timelines and
64 units of times allowed. However, the bits for each coordinate is in fact 
adjustable so long as `X_BITS` and `Y_BITS` are greater than `3` and
 `X_BITS + Y_BITS + T_BITS + L_BITS` is equal to 32.

Moves & Actions
=============

There are altogether three kinds of moves:
+ A `full_move` object (defined in `core/actions.h`) stores two coordinates: where the piece comes from and where it goes to. The name `full_move` is to disambiguate with `std::move()`, a C++ STL function.
+ An `ext_move` object (defined in `core/actions.h`) stores a `full_move` plus which piece it promotes to. If the move is not a pawn/brawn promotion, the promoting piece recorded is usually `QUEEN_W`.
+ A `semimove` object (defined in `core/hypercuboid.h`) stores partial information about a move on a specific timeline. For physical moves, it stores the physical move itself; whereas for jumps, it stores either the leaving part or the arriving part, but not both. Not to play anything on this timeline is also a valid semimove (a `null_move`).
 
The moves can be grouped into actions.
+ A `moveseq` object (defined in `core/actions.h`) is just an array of `full_move`'s.
+ An `action` object (defined in `core/actions.h`) stores a sequence of `ext_move`'s, and also make sure the moves are listed in a standard order.

Check Detection
=============
Check detection is *not* automatically triggered when applying moves or submitting. It is done by calling `find_checks` method (declared in `core/state.h`). Check detection *differs from* checkmate detection.

Checkmate Detection
=============
There are two checkmate detection program: 
1. hc (hypercuboid algorithm), using method from [here](https://github.com/penteract/cwmtt), adapted to c++ with improvements. Some improvements are suggested by the original author.
2. naive, plain DFS searching pruning states with checks/moves not in order.

From my testing, hc has a better worse case performance than naive, especially when the search space is large while available actions are sparse, e.g. when the situation is almost checkmate. However, naive usually perform better when options are abundant.

The hypercuboid algorithm is implemented in `core/hypercuboid.cpp`. It is used in checkmate detection, action generation in `core/gametree.h` and semimove generation in `core/finetree.h`.

Parsing and Printing
=========
The program supports parsing from branched 5dpgn format, whose specification can be found [here](hhttps://github.com/user-attachments/files/26370968/branched.pdf). The syntax tree is declared in `core/ast.h` and the parsing algorithm is implemented in `core/pgnparser.h`.

Recall that a full_move in this program only stores the coordinates where it comes from and goes to. Its `to_string()` output is context-free. `lan(state)` and `pgn(state, options)` use the game state for piece and timeline information; the notation implementation lives in `core/actions.cpp` and `core/actions.inl`.

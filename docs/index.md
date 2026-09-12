Structure of the code
========

Engine documentation:

- [Linear evaluation features](linear-features.md)
- [Frozen Linear profile](linear-trained.md)

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
+ An `ext_move` object (defined in `core/action.h`) stores a `full_move` plus which piece it promotes to. `NO_PIECE` means the choice is unspecified and should be resolved using the state's promotion rules. It remains `NO_PIECE` when promotion is disabled, and canonical non-promotion moves also store `NO_PIECE`.
+ A `semimove` object (defined in `core/hypercuboid.h`) stores partial information about a move on a specific timeline. For physical moves, it stores the physical move itself; whereas for jumps, it stores either the leaving part or the arriving part, but not both. Not to play anything on this timeline is also a valid semimove (a `null_move`).
 
The moves can be grouped into actions.
+ A `moveseq` object (defined in `core/actions.h`) is just an array of `full_move`'s.
+ An `action` object (defined in `core/actions.h`) stores a sequence of `ext_move`'s, and also make sure the moves are listed in a standard order.

Check Detection
=============
Check detection is not automatically triggered when applying moves or submitting.

For general check detection, use `state::find_checks(color)`. It generates every move by `color` that captures an enemy royal piece in the current state.

```cpp
const state &base = /* a state */;
const state &position = /* the state after appling moves in base */;
for (full_move move : position.find_checks(attacker))
{
    // Process a checking move.
}
```

When the position is assembled from boards that have already been constructed, use `check_position` to avoid copying a state and its timeline histories. It is a non-owning view: the constructor copies timeline metadata from a base state, and `add_board()` borrows each new endpoint board. The base state and added boards must remain alive and unchanged while the view is used. `HC_info` uses this interface to check a hypercuboid candidate without applying its moves to a temporary state.

The constructor's `l_min` and `l_max` define the inclusive range of timeline indices that the view may contain. Start with `base.get_lines_range()` and extend it to include every timeline on which a board will be added. A branching superphysical move may place its arrival board on a new timeline outside the base range.

```cpp
const full_move mv = /* a superphysical move */;
const state &base = /* state where the move is applied to */;
const int departure_l = mv.from.l();
const int arrival_l = /* resulting arrival_l */;
const board &departure_board = /* after the piece has gone */;
const board &arrival_board = /* after the piece has arrived */;

const auto [base_l_min, base_l_max] = base.get_lines_range();
const int l_min = std::min({base_l_min, departure_l, arrival_l});
const int l_max = std::max({base_l_max, departure_l, arrival_l});

check_position candidate(base, l_min, l_max);
candidate.add_board(
    departure_l,
    next_turn({mv.from.t(), player}),
    departure_board
);
candidate.add_board(
    arrival_l,
    next_turn({mv.to.t(), player}),
    arrival_board
);

// Above need to be done for all moves in the action to perform

if (auto checking_move = candidate.first_check(attacker, include_physical))
{
    // Process one checking move.
}
```

Use `checks()` instead when every checking move is needed. `check_position` only detects royal captures; it does not apply moves, validate actions, submit turns, or determine which timelines are playable.

#### Check detection on phantom boards

To distinguish checkmate from stalemate when a position has no valid actions, perform check detection after advancing every eligible timeline to its phantom board.

The original way to obtain the boolean result constructs a phantom state and searches it with the general move generator:

```cpp
const bool in_check = position.phantom()
    .find_checks(!position.get_present().second)
    .first()
    .has_value();
```

The check-only interface is:

```cpp
const bool in_check = position.has_phantom_check();
```

Both expressions answer the same question. The original form constructs an owning phantom state and calls `state::find_checks()`. `has_phantom_check()` instead creates a borrowed `check_position` with `check_position::for_phantom()` and stops after finding one royal capture, avoiding the copied multiverse and general move generation. Use the original form when the phantom state or its checking moves are needed for further processing; use `has_phantom_check()` when only the boolean result is needed.

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

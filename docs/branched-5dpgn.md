Branched 5DPGN
==============

Introduction
-----------

The Branched 5DPGN is an extension to Shad's 5D Chess algebraic notation (5DPGN). Compared to Shad's notation, key extensions are:

+ Game tree display
+ Standard ordering of moves
+ Shortened notation
+ Explicit odd/even timeline
+ Game result

Goals
+ accuracy of transcription of game trees
+ ease of reading by a human
+ ease of parsing by a computer program
+ compatibility with Shad's Notation
+ uniqueness of states

The Branched 5DPGN is supported in [this online 5D Chess analyzer](https://ftxi.github.io/5dchess_engine/).

Terminologies
----------

A **turn** is an alternation between White's sub-turn and Black's sub-turn. White or Black's sub-turn is finished once that player submits. The word “turn” is also used to refer to the T coordinate of a board; see below.

A **move** is an act of moving a piece to a legal position.

A **move sequence** is a sequence of moves within a player's sub-turn. If in addition the game can be submitted after playing all moves within this sequence, this move sequence is known as a (legal) **action**. Otherwise, it is an **illegal action**. (Note: This concept is known as “actions” in Shad's notation. Another name is “moveset”.)

A **board** is a two-dimensional chessboard. Each move creates one of two boards. The columns of a board are known as **files**, and the rows of a board are known as **ranks**.

Files are identified by letters a to h, from left to right from the White player's point of view.

Ranks are identified by numbers 1 to 8, from bottom to top from the White player's point of view.

Each square on the board is identified by a unique coordinate pairing, from a1 to h8. The file letter of a square is also known as its **X coordinate**. Likewise, the rank number is also known as its **Y coordinate**.

A **timeline** is a sequence of boards, with White's board and Black's board appearing in turn. A board on a timeline is numbered by its T coordinate (Turn number) and C coordinate (Color).

A **multiverse** is a collection of all timelines. A timeline in a multiverse is numbered by its **L coordinate**. Timelines created by White have positive L coordinates and are located below initial timelines. Timelines created by Black have negative L coordinates and are located above initial timelines.

A timeline is **active** if it is an initial timeline; or, it is the Nth timeline created by one player and the opponent has created at least (N-1) timelines. If a timeline is not active, then it is **inactive**. Both players can submit without playing any move from or to inactive timelines.

The **present** is the minimum turn index of all ends of all timelines. Players can only submit when they are able to shift the present to their opponent.

A **state** consists of this information: a multiverse and the current player. A state is called **nodal** if it is the initial state or it is the state right after any player has submitted. Otherwise it is known as a **temporary state**.

A **playable board** refers to a board with the same color as the current player and located on the end of a timeline. If a board is not playable, then it is called **historical**.

**Physical move** refers to moves within the same board.

**Super-Physical move** or **jump** refers to moves whose destination board is not the same as its source board. A jump is **branching** if it creates a new timeline. Otherwise it is a **non-branching** jump.

When a king is under immediate attack, it is under **check**.

**Checkmate** is a state where one or more kings of the current player are under check and there is no legal action.

**Stalemate** is a state where there is no legal action, but no king is under attack.

**Softmate** is a state where one or more kings of the current player are under check and all legal actions rewind the present.

Coordinate
-----------

The coordinate of a square is written as `(<L>T<T><C>)<X><Y>`, where

+ `<L>` is the L coordinate (timeline number). If the game has odd timelines, `<L>` is an integer. If the game has even timelines, then `<L>` is one of: a non-zero integer, +0, or -0.
+ `<T>` is the T coordinate (turn number). In standard 5D Chess, `<T>` should be greater than or equal to zero.
+ `<C>` is the C coordinate (turn color). It is the letter `b` if the boards on this turn are owned by Black, or the letter `w` if the boards on this turn are owned by White.
+ `<X>` is the X coordinate (file).
+ `<Y>` is the Y coordinate (rank).

The C coordinate may be omitted when the current player is known from context.

The part `(<L>T<T>)` (or `(<L>T<T><C>)`) is referred to as a board's coordinate, while `<X><Y>` is referred to as a square's physical coordinate. `(<L>T<T>)` can also be written with an extra letter L in front, as in `(L<L>T<T>)`.

Overview
------------

A Branched 5DPGN consists of these parts: headers, 5DFEN, and game tree. Example:
```
[Size "6x6"]
[Timelines "Odd"]
[r*nqk*nr*/p*p*p*p*p*p*/6/6/P*P*P*P*P*P*/R*NQK*NR*:0:1:w]

(1w. d3 / b4 
2. N>>e3 )
1w. Nd3 
```

First two lines are the headers, which specify some basic settings of the game. The next line is the 5DFEN, which sets the initial state. The rest is the game tree.

Headers
----------

Metadata of the game is stored as headers. A header looks like this:
```
[HeaderKey "Header Value"]
```

A program that supports Branched 5DPGN should be able to handle these header keys:

+ `Variant`: Which variant is chosen (`Standard Turn - Zero`, `Princess`, etc.)
+ `Size`: Size of a board. Values have the form `<m>x<n>`, e.g. `8x8`, `5x5`, etc.
+ `Timeline`: If the starting state has odd or even timelines. Values are `Odd` or `Even`.

Here are some other recommended headers:
+ `Promotions`: A list of capital letters, describing which piece pawns/brawns can promote to.
+ `Puzzle`: indicates what kind of puzzle it is (mate in N, aid mate, find the best move, etc.)

### Default Behavior

If the variant header is missing, then the starting state should be specified by 5DFEN.

### Notes
If an unknown header key is found, it is recommended to store the data silently instead of raising an error.

This notation standard does not provide a comprehensive description of all variants that should be supported. Therefore, it is recommended to accept size, timeline, and another 5DFEN even if there is already a variant header. This allows writing both variant name and custom information inside 5DPGN, so as to reduce ambiguity.

5DFEN
----------

**5DFEN** stands for 5D Chess Forsyth-Edwards Notation. It consists of several blocks in this form:
```
[<board-string>:<L>:<T>:<C>]
```
`<L><T><C>` are the L, T, and C coordinates respectively. Note: there are no spaces within the block.

Board string consists of the rows of pieces. Rows are listed from top to bottom and separated by `/`.

In each row, the pieces on the board are presented from left to right. Pieces for white are in upper case and pieces for black are in lower case. If a piece is sensitive to whether it is moved or not, it is allowed to add an asterisk `*` to indicate it is not moved. Blanks are presented using numbers starting from 1, where 1 means a blank square, 2 means there are two consecutive blank squares, etc.

Here is the comprehensive list of pieces and whether there is an unmoved version:

| white piece | black piece | Piece Name | Not Moved Piece |
|:-:|:-:|---|:-:|
| P | p | pawn | P*/p* |
| W | w | brawn | W*/w* |
| K | k | king | K*/k* |
| C | c | common king | - |
| Q | q | queen | - |
| Y | y | royale queen | - |
| S | s | princess | - |
| N | n | knight | - |
| R | r | rook | R*/r* |
| B | b | bishop | - |
| U | u | unicorn | - |
| D | d | dragon | - |

Moves
------------

Moves are written in the form of `(0T21)Kc1>>(0T20)b1` (a branching jump in full notation) or `Nf3` (another physical move in short notation).

### Physical Moves (Full Notation)

A physical move usually has the form
```
(<L>T<T>)<Piece><X1><Y1><X2><Y2>
```
If the piece is a pawn, then the piece letter `P` is usually omitted. Thus the syntax is just board coordinate followed by SAN (standard algebraic notation) of traditional chess.

Note that there is no `<C>` coordinate, since the player who is applying this move is implied by context. `<Piece>` should always be written in upper case and files should always be written in lower case letters.

The above move happens on a board with the same color as the current player. It is located on timeline `<L>` and turn `<T>`. On it, a piece of type `<Piece>` is moved from a square on file `<X1>`, rank `<Y1>` to file `<X2>`, rank `<Y2>`.

If the move captures another piece, then it is allowed to place an `x` (the capture symbol) between `<X1><X2>` and `<Y1><Y2>`. Therefore, `(0T1)Rb1xb4` means on board `(0T1)`, a rook moves from square `b1` and takes the piece on `b4`.

### Super-Physical Moves (Full Notation)

A super-physical move has the form:
```
<coordinate-1><jump-indicator><coordinate-2>
```

`<jump-indicator>` should be one of `>` and `>>`, where `>` means that the move is non-branching and `>>` means the move is branching.

The above move means a piece on `<coordinate-1>` is moved to `<coordinate-2>`. Note that if the move is branching, `<coordinate-2>` refers to the square (on a historical board) it arrives on, which is *not* the new board on the new timeline this jump creates.

If this move takes another piece, then the letter `x` should be placed immediately after the jump indicator.

### Action

One player's sub-turn consists of an action to be submitted, which has one or more moves inside. These moves should be separated by whitespace or comments (see below). Every action is prefixed by the turn serial written as `<number>[w|b].`, i.e. a number followed by an optional letter `w` or `b` followed by a dot.

The number starts from `1`, incrementing one after each turn.

The letter indicates who is playing this sub-turn. If no letter is provided, this means White is playing.

The turn serial can be replaced by a slash `/` as shorthand.

Example: The following are equivalent:
+ `1w.(0T1)Pe2e3 1b.(0T1)Ng8f6 2w.(0T2)Pc2c3`
+ `1.(0T1)e2e3 / (0T1)Ng8f6 2.(0T2)c2c3`
+ `/ (0T1)e2e3 / (0T1)Ng8f6 / (0T2)c2c3`

### Castling
Castling moves are written as the movement of the king (rather than the rook). There are also aliases: `O-O` for `Ke1g1`/`Ke8g8` and `O-O-O` for `Ke1c1`/`Ke8c8`.

### Promotion
Use `=<Piece>` to indicate which piece the pawn or brawn promotes to. It should be placed immediately after each move.

Note: a pawn cannot make a super-physical promotion, but a brawn can.

Example: `(0T33)a7a8=Q`

### Check and Mate

Following a physical or super-physical move, it is allowed to use `+` to indicate this is a check. The character `+` should be placed immediately after each move (or promotion, if any).

If the state after submission is checkmate, it is allowed to replace the last `+` in this action with `#`. For softmate, replace the last `+` with `#`.

### Evaluation Symbols

The symbols `!` and `?` may be used to indicate that an action is good or bad, respectively. The symbols `!!`, `??`, `!?`, and `?!` are also permitted, with their meanings as defined in Nunn's convention.

| Symbol | Meaning                    |
|:------:|----------------------------|
| `!`    | good move                  |
| `?`    | bad move                   |
| `!!`   | excellent move             |
| `??`   | blunder                    |
| `!?`   | interesting / speculative  |
| `?!`   | dubious                    |

These symbols can be added at the end of each move. However, if an action consists of multiple moves, it is recommended to use only one evaluation symbol and put it at the end of the last move of the action.

### Comments

Comments are enclosed with curly braces. For comment/uncomment parts of the game tree quickly, it is allowed to have nested comments, as long as `{` and `}` are matched inside the comment.

Comments can be used as separator of moves and turns. However, comments must not appear within a move.

It is encouraged to put comments at the end of an action.

> **Examples:**
> + Good: `(0T1){the starting board}Ng1f3`
> + Bad: `(0T1){the starting board}Ng1f3`.
> + Recommended: `(+0T1)Ng1f3 (-0T1)e2e3 {the first action on two-timelines}`
> + Not recommended: `(+0T1)Ng1f3 {the first action on two-timelines} (-0T1)Pe2e3`

Note: Shad's notation uses `~` to indicate the branching jump rewinds present. It also uses `(~T<T>)(~L<L>)` to indicate an inactive line numbered `<L>`, whose end is on turn `<T>`, is reactivated. I do not find these notations much more useful than a comment `{reactivates line L}`, so I do not intend to include them in this new standard.

### Ordering Moves

Sometimes, one move inside an action depends on another. For example, if an action consists of two branching jumps, then the timeline created by the second jump is farther from initial timelines than the first jump. Therefore, moves are not interchangeable in general.

However, physical moves and non-branching jumps do not rely on anything prior. Therefore they are interchangeable.

The **standard ordering** of moves is performed as follows: given an action, sort all physical moves and non-branching jumps by the timeline it arrives at. The sorting is in increasing order if it is White's sub-turn, and is in decreasing order if it is Black's sub-turn. After applying physical and non-branching moves in this order, apply the branching moves. The resulting state will be identical to the state where all moves are made in the original order.

For ease of implementing branching notation, it is recommended to write an action with moves sorted in standard ordering. (That's why comments are recommended to be written at the end of an action: expect the action to be sorted.)

### Shortened Notation

Design goal: Reading long text feels tiresome. It should therefore allow omitting any kind of data inside a move notation as long as the remaining information is enough to unambiguously tell what the move is.

To precisely describe how much information is enough, we use pattern matching. For physical moves, a pattern is a 6-tuple:
```
(l, t, piece, x1, y1, x2, y2)
```
Here `l` and `t` stand for the board's L and T coordinates respectively. `piece` stands for the piece moved. The physical coordinates of the source square are `x1` and `y1`, while the physical coordinates of the destination square are `x2` and `y2`.

For `l`, `t`, `x1` and `y1`, valid values are corresponding coordinates or a special default value: nil. Value of `piece` is either a capital letter for a piece or nil. However, `x2` and `y2` must be valid coordinates. The pattern is called *full* if all 6 elements of the tuple are non-null.

> **Examples:**
> + `(0,1,N,g,1,f,3)` is full
> + `(0,1,?,?,?,e,3)` is not full

A pattern is considered to *match* a full pattern if whenever the entry of the pattern is not nil, it is equal to the same entry in the full pattern.

> **Examples:** (`?` denotes nil)
> | Pattern | Full Pattern | Match? |
> |---------|--------------|--------|
> | `(0,1,N,g,1,f,3)` | `(0,1,N,g,1,f,3)` | Yes |
> | `(?,?,?,?,?,f,3)` | `(0,1,N,g,1,f,3)` | Yes |
> | `(?,?,?,?,?,f,3)` | `(0,1,P,f,2,f,3)` | Yes |
> | `(0,1,N,?,?,f,3)` | `(0,1,P,f,2,f,3)` | No  |
> 
The example in the last row is not a match because their third entries `N` and `P` are not equal.

For pattern matching related to shortened physical moves, we focus on the skeleton of physical moves, i.e. information related to the piece name and coordinates. Other information, such as capture symbol and check symbol, are beyond the scope of our consideration.

Patterns and skeletons of shortened move notation can be converted to each other naturally. For example, `Nf3` converts to `(?,?,?,?,?,f,3)` and `(0,1,N,g,1,f,3)` stands for `(0T1)Ng1f3`.

To tell whether a shortened move is valid and unambiguous, do this:
+ If the shortened move has non-null piece information, match its pattern against all full patterns of playable moves in the current state. If there is exactly one full pattern that can be matched, then the shortened move is considered valid. If more than one can be matched, the shortened move is ambiguous.
+ If the shortened move's piece entry is nil, we add an extra step. Match the pattern of the shortened move with all playable *pawn move* full patterns first. The shortened move is valid if there is only one match. Otherwise, match the pattern of the shortened move with all playable move full patterns, and examine whether there is exactly one match.

This is all for physical moves. For super-physical moves, the process is basically the same except that the pattern is changed to a 10-tuple:
```
(l1, t1, piece, x1, y1, jump-indicator, l2, t2, x2, y2)
```
All entries except for `x2` and `y2` are nullable. Note that jump indicator (`>` or `>>`) is used in pattern matching (so `Q>f7` and `Q>>f7` are two different moves).

### More on Shortened Notation

The shortened notation is designed as pattern matching rather than a specific ruleset because 5D Chess is more complicated than normal chess. For a move, there is possibly more than one way to simplify. It is hard to convince others why one way is better than another way.

Nevertheless, here is some non-mandatory guidance:
1. Prefer omitting the piece name for pawn.
2. For physical pawn capture, whenever capture symbol is used, always include the source file. Write 'bxc4' rather than 'xc4'.
3. For pawn jump, always include the source file. Write 'a>>a2' rather than '>>a2'.
4. When the turn number is omitted, place an extra letter L inside parenthesis. Write `(L2)c3` rather than `(2)C3`.

Final note on extra information: Symbols indicating capture, check, and similar annotations, if present, should be used correctly. These symbols are not considered by the pattern-matching process, which is defined solely for disambiguation. Implementations supporting Branched 5DPGN may, at their discretion, reject inputs in which such symbols are used incorrectly.

Game tree
------------

Theoretically, a 5D Chess game tree is a tree with nodes being the nodal states. For each node, its children are states obtained by applying an action and submitting. These children are called **variations**.

This section focuses on how to represent a game tree in our notation.

Using tree-shaped traversals of game states is useful in studying 5D Chess. Possible applications are:
+ Opening Book: the openings of a game form a tree with the root being the starting state.
+ Solved Variants: some variants are determined, meaning that either White or Black has a winning strategy. In other words, no matter what the opponent responds, the player can play according to the strategy to force a win. The strategy itself forms a game tree.
+ Puzzles: In mate-in-N puzzles, the players have a winning strategy, which is a game tree with at most 2N-1 layers.

This defines a syntax for writing a game tree as text. A game tree is expressed as a sequence of variation blocks. Variation blocks are written as parenthesized groups like `(action subtree)`, which serve to clearly separate alternative branches. Actions need to be numbered. `subtree` follows the same syntax as a game tree. Any number of these parenthesized blocks may appear. For the last variation block, the parentheses enclosing the block are optional. If subtree starts with `(`, the space between action and subtree is optional. You may insert space and comments between parentheses and action/subtree.

> **Examples:**
> + `1.e3 / Nf6 2.Nf3 / d5` is equivalent to `1.e3 ( / Nf6 ( 2.Nf3 ( / d5)))`. They represent the tree
> ```
> 1. e3
> └── / Nf6
>     └── 2. Nf3
>         └── / d5
> ```
> + A game tree in general looks like this:
> ```
> 1.e4 / e5
> (2. xx / xx (3. xx / xx) 3. xx / xx)
> (2. xx / xx)
> 2. xx / xx
> 3. xx
> ```
> The tree it represents is:
> ```
> 1. e4 / e5
> ├── 2. xx / xx
> │   ├── 3. xx / xx
> │   └── 3. xx / xx
> ├── 2. xx / xx
> └── 2. xx / xx
>     └── 3. xx
> ```

Note:

1. The branched syntax is compatible with the non-branched syntax. In fact, a game (no branch occurs) can be represented as a game tree with degree less than or equal to one, therefore it does not require any parenthesized group for variations.
2. The lichess study file for traditional chess uses a different grammar: it puts branches after the first action of first variation. Since 5D Chess notation is much longer, it is not intuitive to track what the previous move is before the variation begins. Therefore, our standard decides to use another grammar.

### Deduplication

In the notation, for each node of the game tree, child states of that node should be mutually different in principle.

> **Example:** In this setting:
> ```
> [Variation "Two Timelines"]
> [Timeline "Even"]
> [r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:+0:1:w]
> [r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:-0:1:w]
> (1. (L+0)e3 (L-0)Nf3)
> 1. (L-0)Nf3 (L+0)e3
> ```
> Either playing `(L+0)e3` or `(L-0)Nf3` first results in the same state.

If duplication occurs, programs supporting Branched 5DPGN ensure only one variation starting with that action remains. The exact behavior is implementation-defined.

The standard ordering of moves inside an action makes it easy to remove duplicated variations. 

> In the example above, both variations are sorted to `(L-0)Nf3 (L+0)e3`. Therefore the first variation may be overwritten by the second variation.

Note: deduplication is designed for game-analyzing software to make sure when a user clicks the pieces to reconstruct a situation, it revisits existing game trees whenever possible.

### Game Result

In the game tree syntax, a subtree may be replaced by `1-0`, `0-1`, or `1/2-1/2` to denote that this branch stops here with White winning, Black winning, or a draw respectively.

Formal Definition
-----------------

Here is the BNF of Branched 5DPGN:

```bnf
<game> ::= {<space-comment>* <header>}* {<space-comment>* <board-fen>}* <space-comment>* <game-tree>
<header> ::= '[' <header-key> <whitespace>+ '"' <header-value> '"' ']'
<header-key> ::= 'Size' | 'Promotions' | 'Date' | 'Round' | 'White' | 'Black' | 'Result' | any string not containing whitespace or '"'
<header-value> ::= any string not containing '"'
<board-fen> ::= '[' <fen-string> ':' <line> ':' <time> ':' {'w' | 'b'} ']'
<fen-string> ::= any string not containing ':' or ']' ;to be parsed separately

<game-tree> ::= {'(' <space-comment>* <action> <space-comment>* <game-tree> ')' <space-comment>*}* [<action> <space-comment>* <game-tree> <space-comment>*] | <result>

<action> ::= <turn-serial> <space-comment>* <move> {<space-comment>+ <move>}* <space-comment>*
<turn-serial> ::= <natural-number> ['w' | 'b'] '.' | '/'
<space-comment> ::= <whitespace> | <comment>
<comment> ::= '{' <comment-text> '}' <whitespace>*
<comment-text> ::= any string with balanced '{' and '}'

<move> ::= <physical-move> | <super-physical-move>
<physical-move> ::= [<board>] ([<piece-name>] [<file>] [<rank>] ['x'] <file> <rank> ['=' <promote-to>] | 'O-O' | 'O-O-O') [<check-symbol>] [<evaluation-symbol>]
<super-physical-move> ::= [<board>] [<piece-name>] [<file>] [<rank>] (<jump-indicator> ['x'] | [<jump-indicator>] ['x'] <board>) <file> <rank> ['=' <promote-to>] [<check-symbol>] [<present-moved-symbol>] [<evaluation-symbol>] <timeline-comment>*
<board> ::= '(' (['L'] <line> | [['L'] <line>] 'T' <time>) ')'
<piece-name> ::= 'P' | 'W' | 'K' | 'C' | 'Q' | 'Y' | 'S' | 'N' | 'R' | 'B' | 'U' | 'D'
<promote-to> ::= 'Q' ;change this if promotion to other pieces is allowed
<jump-indicator> ::= '>' | '>>'
<file> ::= 'a' | 'b' | 'c' | 'd' | 'e' | 'f' | 'g' | 'h'
<rank> ::= '1' | '2' | '3' | '4' | '5' | '6' | '7' | '8'
<check-symbol> ::= '+' | '#' | '*'
<evaluation-symbol> ::= '!!' | '!' | '??' | '?' | '!?' | '?!'
<present-moved-symbol> ::= '~'
<timeline-comment> ::= <whitespace> '(' {'>L' <line> | '~T' <time>} ')'
<line> ::= ['+' | '-'] <natural-number>
<time> ::= <natural-number>
<natural-number> ::= '0' | <positive-number>
<positive-number> ::= '1' | '2' | '3' | '4' | '5' | '6' | '7' | '8' | '9' | '10' | ...
<whitespace> ::= {' ' | '\t' | '\n' | '\r'}+

<result> ::= '1-0' | '0-1' | '1/2-1/2'
```

And here is the BNF of 5DFEN:
```bnf
<board-fen> ::= '[' <fen-string> ':' <line> ':' <time> ':' {'w' | 'b'} ']'
<fen-string> ::= {<piece> | <blanks> | '/'}*
<piece> ::= <white-piece> | <black-piece>
<blanks> ::= '1' | '2' | '3' | '4' | '5' | '6' | '7' | '8'
<white-piece> ::= 'P' | 'W' | 'K' | 'C' | 'Q' | 'Y' | 'S' | 'N' | 'R' | 'B' | 'U' | 'D' | 'P*' | 'W*' | 'K*' | 'R*'
<black-piece> ::= 'p' | 'w' | 'k' | 'c' | 'q' | 'y' | 's' | 'n' | 'r' | 'b' | 'u' | 'd' | 'p*' | 'w*' | 'k*' | 'r*'
```

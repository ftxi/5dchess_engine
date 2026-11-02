# 5D Chess mechanics

Gameplay and semantics reviewed by the maintainer; published in the repository
on 30 October 2026. Verification scope and remaining work are recorded in section 13.

## 1. How to read this specification

Start with Part I for the gameplay account. It explains what players can do in
ordinary chess language, including whether a pawn has moved and whether castling
or en passant is available.

Part II gives precise rules for board history, coordinates, complete actions,
and custom positions. Part III explains how this engine represents those rules,
provides the softmate proof, and records the sources and limits of verification.
The game's rules do not require a particular data structure or position notation.

Making one move, submitting a complete turn, and testing check are separate
operations. Keeping that distinction clear is essential in all three parts.

## Part I: Gameplay in brief

5D Chess is a two-player, perfect-information game played by White and Black.
Both players can see the entire game state, including all recorded boards.

Players take turns performing an **action**. An action consists of one or more
**moves**, followed by **submission**. Making a move does not necessarily finish
the turn: a player may need to coordinate moves on several boards before
submitting.

Like ordinary chess, pieces move according to their types, capture opposing
pieces, and must protect their king. The difference is that play takes place
across multiple boards arranged into timelines. Pieces can travel between boards,
including into the past, and some moves create new timelines. Earlier boards
remain part of the game.

The supported pieces are:

| Ordinary chess pieces | Additional pieces |
| --- | --- |
| King | Common king |
| Queen | Royal queen |
| Rook | Princess |
| Bishop | Unicorn |
| Knight | Dragon |
| Pawn | Brawn |

Kings and royal queens are **royal pieces**: they must be protected from capture.
A common king moves like a king but is not royal. The movement rules for every
piece are described below.

Winning requires checkmating the opponent: leaving them in check with no legal
action. The following sections explain how boards, timelines, and submission
affect that familiar objective.

### Boards and turns

A game contains several timelines. Each timeline is a sequence of chessboards
showing its history; its latest board is its endpoint. Every board says whether
White or Black would move next there. Earlier boards remain available as
travel destinations, but pieces move only from endpoints.

You may move your pieces from any endpoint
where it is your turn, including endpoints on inactive timelines or ahead of the
Present. You remain the player taking the turn until you submit the whole action.

### The Present

The Present is the earliest endpoint among the active timelines, taking account
of whose turn it is there. The central timeline or timelines are active. Of the
timelines each player creates, at most one more than the other player's total
are active; extra timelines are inactive, starting at the outer edge. Creating
a timeline can therefore activate one of the opponent's existing timelines.
Inactive timelines still exist and can be used for moves and attacks.

To finish your turn, the Present must be on the opponent's turn, and the opponent
must have no immediate capture of any of your kings or other royal pieces from
their available endpoints. You can make several moves to achieve this. Except for
castling's own restrictions, royal safety is judged after the complete action.
The Present may move into the past, so finishing a turn need not advance time.

### How pieces travel

Pieces can move through four directions of space and time: files, ranks, turns,
and timelines. A step in time goes between boards where the same player is to
move, so one time step is a full turn. Every destination board must already exist
and have the same player to move as the source board.

| Piece | Movement |
| --- | --- |
| Rook | Any distance along one direction |
| Bishop | Equally far along two directions at once |
| Queen | Equally far along any nonempty choice of the four directions |
| King | One step along any nonempty choice of directions |
| Knight | Two steps along one direction and one along another, jumping over obstacles |

A sliding piece needs all intermediate squares to exist and be empty. A missing
board stops its path. You cannot land on your own piece or a wall. Landing on an
enemy piece captures it in the resulting board. The additional pieces have the
movement rules in section 5.

A pawn moves one square forward along its rank, or one timeline toward the
opponent's side. A pawn that has not previously moved may instead move two steps
along either of these directions, provided both squares are empty. It cannot
combine a rank step with a timeline step for this double move.

To capture, a pawn moves one file sideways and one rank forward, or one timeline
forward and one turn into either the past or the future. Its destination must
contain an enemy piece, except when capturing en passant.

A brawn generalizes the pawn: it keeps the pawn's ordinary moves and gains more
captures. It can capture one step diagonally along any two directions, provided
at least one is forward along its rank or toward the opponent's timelines, and
neither is backward along its rank or toward its own side of the timelines.
This restriction concerns rank and timeline direction; captures into the past
are still possible.

### Moving between boards

An ordinary move on one board creates the next board on that timeline. The
previous board remains as history.

Travel to another endpoint advances both timelines: one new board shows the
piece leaving, and the other shows it arriving. Travel to a historical board
instead creates a new timeline with the arrival position. The original history
is unchanged. White creates timelines on White's side and Black on Black's side.
The new timeline starts with the arrival board; it does not contain a copied
sequence of earlier boards.

The arrival board is one half-turn after the board targeted. All the other
pieces keep their positions and first-move rights from that target board. The
traveling piece has now moved, even if its square on the arrival board has the
same file and rank as its departure square.

Move order matters: a destination can become historical earlier in your action,
and the order in which you create timelines determines their arrangement.

### Special moves

**Castling.** A king may castle with a friendly rook if neither has previously
moved and the route between them is clear. The king moves two files toward the
rook, and the rook finishes next to it on the inner side. On the original board,
the king's starting, crossing, and landing squares must not be attacked by an
enemy piece on that board. The completed action must also satisfy royal safety
across timelines. Custom placements and exact distances are covered in section 5.6.

**En passant.** Immediately after an enemy pawn makes its first move two ranks
forward on a timeline and lands beside your pawn, your pawn may capture it as
though it had advanced only one rank. This opportunity belongs to the next move
on that timeline. It does not apply to a two-timeline move. Brawns can take part
as either the capturing or captured piece.

**Promotion.** A pawn reaching its last rank by a move on the same board promotes
to a piece allowed by the position's promotion rules; the default is a queen.
A pawn traveling between boards does not promote. A brawn also promotes when
its arrival from another board is on its last rank. Promotion can be disabled.

Whether a piece has moved is a fact about its history on that board. Moving a
piece now does not change the history or movement rights of its earlier copies.
For a custom starting position, the supplied movement rights take the place of
any omitted history.

### Check and the end of the game

At the beginning of your turn, you are in check if forcibly skipping all active
timelines at the Present would let the opponent capture one of your royal pieces.
Forcibly skipping means advancing each of those boards by one half-turn without
moving any pieces. It is a hypothetical test, not an
action players may take. Other boards remain unchanged.

The capture test includes attacks from all opponent endpoints where it is their
turn, even on inactive timelines or ahead of the Present. It also includes
captures of historical royal pieces. It asks whether a piece could make the
capture, without requiring a complete legal action by the attacker.

If you have no legal action, you are checkmated when in check and stalemated
otherwise. Under this engine's chosen meaning of softmate, you are in check
and have legal actions, but all of them move the Present backward. Softmate
allows play to continue; community usage of that term is not uniform.

A custom position need not be reachable from an opening. It must have a valid
board-and-timeline structure and must not already allow either player to capture
an opposing royal piece immediately. That restriction concerns the actual
position, whereas check uses the hypothetical skipping test.

## Part II: Precise semantics and examples

### 2. Boards, timelines, and players

Every board has the same width `X` and height `Y`. A square has coordinates
`(x,y)`, with `a1 = (0,0)`. Increasing `y` is White's forward rank direction.
The current implementation supports dimensions up to 8 by 8.

A board also has a timeline coordinate `l`, a turn number `t`, and a board-side
`c`, either White or Black. The board-side says whose move would advance that
board. It does not restrict which colors of pieces can be on it.

Write a board's **phase** as `(t,c)`. Phases are ordered as follows:

```text
(t, White), (t, Black), (t+1, White), (t+1, Black), ...
```

Equivalently, a phase has index `2t+c`, where White is 0 and Black is 1.
Advancing a board by one half-turn means taking the next phase in this order.
The implementation uses nonnegative `t`.

Each timeline consists of a finite, consecutive sequence of boards. Its last
board is its **endpoint**. Earlier boards on that timeline are **historical**.
Timelines need not begin or end at the same phase. A newly created timeline can
initially contain just one board.

Once a board has been created, making a move does not rewrite it. New board
versions are appended or placed on a new timeline. The multiverse is the
collection of all these timelines and their boards.

A square contains an empty space, a colored piece, or a wall. Walls block movement
and cannot be captured. A position also records the movement rights needed to
determine first pawn moves, castling, and en passant.

A king and a royal queen are **royal pieces**. A common king is not royal.
There can be more than one royal piece on a board. Royal safety concerns all
of the player's royal pieces, including relevant historical copies.

#### 2.1 Movement history and supplied rights

“Has not previously moved” refers to the piece as recorded on the particular
board being used. It is not a global fact about every copy of that piece in the
multiverse. Its first-move right is available until it moves, assuming that right
was present in the initial setup.

When a piece moves, its resulting version counts as having moved, including
travel that leaves its file and rank unchanged. Earlier boards keep their own
history. Copying a historical board to create a branch preserves the first-move
rights of its other pieces as they were on that board. For example, a rook that had not
moved at the historical destination can still be eligible to castle on the new
branch, even if a later copy moved on the original timeline.

A custom setup can supply movement rights without supplying an opening history
that explains them. Such supplied rights determine first-move and castling
eligibility; placement alone does not establish them. En passant additionally
uses the preceding board on the same timeline, as detailed in section 12.2.
These declarations do not assert that the custom history is reachable by legal
play. No move restores a first-move right to the piece being moved, and promotion
does not grant one to the promoted piece.

### 3. Timeline coordinates and activity

#### 3.1 Coordinates

Timeline coordinates are consecutive integers. White creates new timelines at
the positive end; Black creates them at the negative end. There are two layouts:

| Layout | Central timelines | Reflection across the center |
| --- | --- | --- |
| Odd | `0` | `mirror(l) = -l` |
| Even | `-0` and `+0` | `mirror(l) = -l-1`, using internal integer coordinates |

For the even layout, displayed `-0` has internal coordinate `-1`, and displayed
`+0` has internal coordinate `0`. Displayed `-1` has internal coordinate `-2`.
Thus `-0` and `+0` are one timeline step apart. Movement distances always use the
consecutive internal coordinates, not arithmetic on the printed labels.

The central timelines are what the implementation calls the initial timeline
range. Supplying more timelines in a custom starting position does not make all
of them central timelines.

#### 3.2 Active timelines

Activity determines which endpoints contribute to the Present. In the ordinary
centered layouts, the rule is that each player can have at most one more active
created timeline than the total number created by the other player. Extra
timelines on the more numerous side are inactive, starting at the outer edge.

For example, in an odd layout with timelines `0,1,2,3`, only `0,1` are active.
If Black creates `-1`, then `-1,0,1,2` are active: creating one timeline has also
activated an existing timeline on the opposite side.

Here is the exact calculation, including the layout convention. Let the existing
timeline range be `[l_min,l_max]`:

```text
radius = min(mirror(l_min), l_max) + 1
active_min = max(l_min, mirror(radius))
active_max = min(l_max, radius)
```

Every timeline in `[active_min,active_max]` is active. Activity is recalculated
when a timeline is created. An inactive timeline still exists, can contain
attacking pieces, and can be involved in a move.

### 4. The Present and the player taking a turn

#### 4.1 Game rule

The **Present** is the earliest endpoint phase among the active timelines.
Inactive endpoints do not participate in this minimum. At the beginning of a
turn, the player taking the turn is the board-side of the Present.

Recalculate the Present after each move from the resulting active endpoints.
The player taking the turn remains the same until submission, even when the
Present changes or acquires the opponent's board-side. Its board-side is one
condition for submission, as described in section 8; it does not automatically
end the current player's action.

An endpoint is available as a move source whenever its board-side is the player
taking the turn. It need not be at the Present, and its timeline need not be
active. An opponent-sided endpoint is unavailable as a source for this player.
An earlier board is never a move source.

In particular, an opponent-sided endpoint is not thereby historical: a historical
board has a later board on the same timeline.

### 5. Piece movement

#### 5.1 Coordinates of a move

A move starts at an available endpoint and uses a piece of the player taking
the turn. Its destination board must exist and have the same board-side as the
source. Within that board, its destination square must exist.

Use the displacement

```text
(dx, dy, dt, dl)
```

for changes in file, rank, turn number, and timeline. `dt = 1` means one full
turn, between boards with the same board-side. It does not mean one half-turn.

A **physical** move has `dt = dl = 0`. A **superphysical** move changes time,
timeline, or both. Traveling toward a later `t` can be possible on another
timeline, even though there is no later board beyond the source timeline's
endpoint.

Except for the special moves described below, a destination must be empty or
contain an enemy piece. Landing on an enemy piece captures it in the resulting
board. A friendly piece or wall prevents landing.

#### 5.2 Ordinary and fairy pieces

For a sliding move, choose a nonzero direction whose components are each
`-1`, `0`, or `1`, and a positive integer distance. The piece follows that
direction for that distance. Every intermediate square must exist and be empty.
In particular, an absent intermediate board interrupts a sliding move; it is
not an empty board through which a piece can travel.

| Piece | Allowed displacement |
| --- | --- |
| Rook | Sliding along exactly one of the four axes |
| Bishop | Sliding along exactly two axes, equally far on each |
| Unicorn | Sliding along exactly three axes, equally far on each |
| Dragon | Sliding along all four axes, equally far on each |
| Queen or royal queen | Any of the preceding four sliding patterns |
| Princess | Rook or bishop patterns |
| King or common king | One step along any nonempty selection of axes |
| Knight | Exactly two nonzero components, with absolute values 1 and 2 |

A knight does not require intermediate squares or boards to exist. Its landing
board and square must exist. Kings and common kings have the same ordinary
movement; being royal affects turn completion, not their displacement pattern.

#### 5.3 Pawns and brawns

Let `f = +1` for White and `-1` for Black be the forward rank direction.
Let `g = -1` for White and `+1` for Black be the forward timeline direction,
toward the opponent's side of the multiverse.

Both pawns and brawns can move without capturing by either displacement:

```text
(0, f, 0, 0)       (0, 0, 0, g)
```

A pawn or brawn that has not previously moved can instead go two steps along
either of these directions. Both the intermediate and destination squares must exist and be
empty. There is no mixed rank-and-timeline double move.

Their ordinary capture displacements are:

```text
(+1, f, 0, 0)     (-1, f, 0, 0)
(0, 0, +1, g)    (0, 0, -1, g)
```

A brawn additionally has the following captures:

```text
(+1, 0, 0, g)    (-1, 0, 0, g)
(0, f, 0, g)     (0, f, -1, 0)
```

Each listed capture requires an enemy piece at the destination. These formulas
use the coordinates above; pictorial notions such as “up a timeline” should not
replace the signs.

#### 5.4 En passant

En passant is a physical capture by a pawn or brawn of an enemy pawn or brawn.
In an ordinary legal history it captures a neighboring piece that just made
a two-rank move on the same timeline. There is no corresponding en passant
capture for a two-timeline move.

For supplied histories that are not known to arise from legal play, section 12.2
defines how eligibility is determined from the recorded boards.

#### 5.5 Promotion

A pawn or brawn making a physical move to its last rank promotes if promotion
is enabled. A brawn making a superphysical move also promotes when its destination
is on its last rank. A pawn's superphysical move does not promote it.

The last rank is `Y-1` for White and `0` for Black. The condition is the
destination rank; it does not additionally require a change in rank.

Promotion choices are a position setting. Supported choices are queen, rook,
bishop, knight, unicorn, dragon, princess, and common king. The default setting
allows only a queen. If promotion is disabled, the piece remains a pawn or brawn.
A promoted rook is not eligible to castle.

#### 5.6 Castling

A royal king can castle physically with a friendly rook on the same rank if
neither has previously moved. The rook must be the first occupied square in that direction, with
its file coordinate differing from the king's by at least two. The king
moves two files toward the rook; the rook moves to the square one file from the
king's original square in that direction. Both pieces now count as having moved.

The king's start, crossing, and landing squares must not be physically attacked
on the original source board. The king's destination must be within the board.
There is no requirement that a custom position use the standard starting files
or back rank. A rook two files away can occupy the king's eventual landing square
before the special relocation.

This same-board test is intentional and has been confirmed by the maintainer.
It does not itself test superphysical attacks on the king's start, crossing, or
landing square. The completed turn must still satisfy the full royal-safety
condition in section 8. The local castling restriction and final royal safety
are separate requirements.

### 6. What a move does to the boards

First determine the source and destination in the position before that move.
Then apply one of the following three cases. All existing boards remain intact.

#### 6.1 Physical move

Append one board to the source timeline, at the phase immediately after its
endpoint. It is the source board with the move made, including any capture,
promotion, castling, or en passant changes.

#### 6.2 Superphysical move to an endpoint

If the destination is another timeline's endpoint:

1. Append a successor of the source board, with the moving piece removed.
2. Append a successor of the destination board, with the arriving piece placed
   on its destination square, replacing a captured enemy piece if any.

No timeline is created. Both boards advance one half-turn, even when their turn
numbers differ. The arriving piece counts as having moved; the other pieces
retain their first-move rights from the destination board.

#### 6.3 Superphysical move to a historical board

If the destination board has a successor on its timeline:

1. Append a successor of the source board, with the moving piece removed.
2. Create a new timeline containing a successor of the destination board, with
   the arriving piece placed on its destination square and any capture or
   promotion applied.

White's new timeline has coordinate `l_max+1`; Black's has `l_min-1`. The original
destination timeline is unchanged. The new timeline does not acquire a sequence
of copied ancestral boards: its first board is this arrival board.

Recalculate activity and the Present. The player taking the turn remains the
same. Section 12.1 describes how the engine updates its corresponding bookkeeping.

For example, White arriving at historical `(t,White)` creates an arrival board at
`(t,Black)`. Black arriving at historical `(t,Black)` creates one at
`(t+1,White)`. These phase changes are separate from the move's geometric `dt`.

#### 6.4 Move order matters

A turn is an ordered sequence of moves. A board that is an endpoint before one
move may become historical after it. A later move to that same board will then
branch. Also, branch order determines which new timeline receives which board,
and therefore changes distances and possible attacks along the timeline axis.

The engine may normalize action notation, but a mathematical account must not
assume arbitrary move permutations have the same effect.

### 7. Royal exposure: the capture test

Before defining a legal completed turn or check, define this common operation:

> A color exposes the opponent's royal pieces if a piece of that color can
> capture an opponent royal piece from an endpoint whose board-side is that color,
> using the movement and obstruction rules.

Call this predicate `capture_royal(M, attacker)` for a multiverse `M`.

The capture test has several important boundaries:

- Sources are endpoints of the attacker's board-side, on **all** timelines.
  They need not be active or at the Present.
- The target board has the attacker's board-side, as in ordinary move geometry.
  It may be an endpoint or a historical board.
- The target is an enemy king or royal queen. Attacking a common king does not
  count as royal exposure.
- The test asks whether the capture is geometrically possible. It does not ask
  whether the attacker could submit a whole legal action containing it, and
  does not recursively check the attacker's own royal safety.

For example, an attacker pinned to its own royal piece still contributes an
attack in this test. A nonroyal friendly piece can block a ray toward its own
king; its ownership prevents it from attacking that king itself. Such a blocker
can nevertheless have other attacks or movement possibilities relevant elsewhere
in the position.

### 8. Completing a turn

Individual moves are checked for source availability, piece movement, obstruction,
and special-move conditions. Except for castling's specific local restrictions,
the mover's royal safety is checked when the turn is submitted, not after every
individual move. A partial turn can therefore contain an exposure that another
move in the same turn resolves.

To submit a turn, both conditions must hold:

1. The Present has the opponent's board-side.
2. In the actual resulting multiverse, the opponent has no capture of one of the
   mover's royal pieces according to section 7.

If submission succeeds, the other player takes the next turn at that Present.

The first condition compares **board-sides**, not turn-number inequalities.
Because branching can move play into the past, a legal turn need not advance the
Present numerically beyond its original phase.

The second condition uses the actual boards after the moves. It does **not**
forcibly skip boards. The player's moves have already produced the boards on
which the opponent can respond. Submission asks whether those actual responses
include a capture of the mover's royal pieces.

By contrast, check is assessed before the player makes their action. On a board
where it is still that player's turn, an opponent piece cannot yet move from
that endpoint. Forcibly skipping the active endpoints at the Present exposes
the threats that would exist if the player left their pieces in place. That
hypothetical position defines check; it does not replace the position produced
by a real action.

For example, on a single White-to-move board, a black rook can threaten the white
king even though Black cannot yet move from that endpoint. The check test copies
the position into a hypothetical Black-to-move board. After White actually moves,
submission instead examines the Black-to-move board produced by that move.

In pseudocode, where `p` is the player taking the turn:

```text
submit(M, p):
    q = earliest endpoint phase among active timelines of M
    if q.side == p:
        fail
    if capture_royal(M, opponent(p)):
        fail
    succeed with multiverse M and next player q.side
```

A **legal action** is a finite ordered sequence of permitted moves followed by
a successful submission. The existence of a legal action is defined this way;
it does not depend on how the engine searches for one.

### 9. Check, checkmate, and stalemate

The definitions in this section apply at the start of a player's turn. Partial
actions are governed by the move and submission rules in sections 5–8.

A player is in **check** if forcibly skipping all active timelines at the Present
would allow the opponent to capture one of that player's royal pieces.

Forcibly skipping a timeline means advancing its endpoint by one half-turn
without moving any pieces. The new board preserves piece placement and first-move
rights. This is only a way to determine check; players cannot actually skip a move.

Here “at the Present” means that the endpoint phase equals the Present,
including its board-side. Choose all such active endpoints in the original
position and skip them together in the hypothetical position. Other timelines
remain unchanged. Do not repeatedly recompute the Present and skip more boards.

After these hypothetical skips, use the capture test of section 7 with the
opponent as attacker. Its sources and targets are not restricted to the boards
just added. In particular, existing opponent-sided endpoints elsewhere still
participate in the test.

At the start of a player's turn:

| Is there a legal action? | Is the player in check? | Result |
| --- | --- | --- |
| Yes | Either | Neither checkmate nor stalemate |
| No | Yes | Checkmate |
| No | No | Stalemate |

These definitions can be summarized without introducing a second kind of legal
action:

```text
in_check(S) = capture_royal(skip_active_present(S), opponent(player(S)))
checkmate(S) = no legal action exists from S, and in_check(S)
stalemate(S) = no legal action exists from S, and not in_check(S)
```

The check test skips only active endpoints at the Present. Replacing
that with “all active endpoints” or “all endpoints of the player's board-side”
changes the definition. This distinction is central to the documentation update
that motivated this specification.

#### Softmate: the definition adopted by this engine

The engine also reports **softmate**. Community definitions differ. The definition
chosen for the updated engine is:

> A player is in softmate if they are in check and have at least one legal action,
> but every legal action moves the Present backward.

Compare the full Present phase after submission with the phase at the start of
the turn, including board-side. Here the Present means the earliest active
endpoint phase. Softmate has legal escapes, so it is not checkmate under the
definition above. A piece traveling backward does not by itself establish
softmate: its destination might still be ahead of the original Present.

Section 12.3 proves the correspondence between this definition and the reviewed
implementation, under explicitly stated assumptions about the legal-action search.

### 10. Worked consequences and review examples

These are deductions from the rules and source inspection, not a claim that an
independent reference implementation has executed all of them. Local piece
examples specify only the pieces relevant to their conclusion; they are not
assertions that the entire position is reachable from an opening or is an
admissible custom input. In particular, the unblocked attack in section 10.3 is
a geometric example that can occur in a hypothetical position or partial action;
it would violate the import restriction if supplied as an initial position.

#### 10.1 Ordinary check is recovered

Suppose the only timeline ends on Black's board-side, with a black king and a
white rook attacking it along an unobstructed rank. Forcibly skipping that board
creates an unchanged White-sided endpoint. The rook can now capture the black
king in the capture test. Thus Black is in check. Whether this is checkmate still
depends on the existence of a complete legal action.

#### 10.2 An inactive early board does not define the Present

Use the odd layout with timelines `0,1,2`. The active timelines are `0,1`.
Let their endpoints be `(5,White)` and `(6,Black)` respectively; let timeline
`2` end at `(2,White)`. The Present is `(5,White)`, not `(2,White)`.
White can nevertheless use the endpoint on timeline `2` as a move source.
For the initial check test, only timeline `0` is forcibly skipped.

#### 10.3 A timeline-axis rook attack and its blocker

Fix a turn, board-side, and square. Suppose the corresponding boards exist at
every timeline coordinate from `a` through `c`, with `a<c`. Put a white rook
at coordinate `a`, a black king at `c`, and no intervening occupied square on
that ray. If the rook's board is a White-sided endpoint, it attacks the king.

Placing any nonroyal black piece at coordinate `b`, where `a<b<c`, blocks this
particular attack. Placing it outside that interval does not. Reversing the
order gives the same betweenness condition. If an intermediate board is absent,
the attack is already interrupted, even without a piece there.

This is the local observation behind the proposed BETWEENNESS encoding. It is
not a complete reduction: one must also force the board choices and ordering,
exclude other legal escapes and attacks, and establish a polynomial construction.

#### 10.4 A branch copies a position, not its whole timeline

White travels from an endpoint to a historical White-sided board at turn 3.
The source receives a Black-sided successor with the traveler removed. The new
timeline begins at `(3,Black)` with the arrival position. It has no `(3,White)`
board or earlier boards of its own. A proposed attack passing through an absent
earlier board of that new timeline therefore fails.

#### 10.5 A finite bound on the length of one action

Fix the player taking the turn. Each move consumes at least one endpoint of that
player's board-side. Every board it creates has the opponent's board-side, so it
creates no new source endpoint for the same player. A move to another endpoint
consumes two such source opportunities; a branch adds an opponent-sided endpoint.

Consequently, an action contains at most as many individual moves as there were
timelines at its start. This is a derived fact, not an extra restriction on play.
It is useful when discussing certificates of legal-action existence. It does
not by itself prove any NP-completeness claim.

### 11. Custom positions and mathematical generalizations

#### 11.1 Chosen domain

This specification admits structurally valid custom positions for which

```text
not capture_royal(M, White) and not capture_royal(M, Black)
```

The capture test is the one in section 7. It examines endpoint-based captures
without hypothetical skipping and without requiring a whole legal action by the
attacker. Thus a position may be in check under section 9 and still be admissible:
check tests a different multiverse, obtained by forcibly skipping boards.

Reachability from a standard or other specified opening is not required. This
is the domain chosen by the maintainer, matching the checked loader's initial
royal-exposure restriction. `UNSAFE` mode can bypass that restriction, but does
not enlarge the input domain chosen here.

This restriction is on admitted positions, not a condition to reapply after
each individual move within an action. Partial actions can contain temporary
royal exposure; hypothetical positions used to test check deliberately allow it.
Those intermediate constructions are governed by the transition and submission
rules rather than the initial-position import check.

#### 11.2 Structure, loading, and reachability

Three different questions should be kept separate:

1. Does a supplied position have the required structural form?
2. Does it satisfy the loader's checks?
3. Can it arise by legal play from a specified opening?

The representation has common board dimensions, consecutive timeline
coordinates, and consecutive phases within each timeline's existing history.
Starts may differ between timelines. A custom history need not come with moves
that explain every transition between its supplied boards.

The checked position loader checks initial royal exposure for both colors and
validates the subsequently replayed actions. This is not a proof that the supplied
initial history is reachable. Direct state construction and unsafe loading paths
must not be conflated with the checked loader.

#### 11.3 Mathematical generalizations

A complexity theorem using the chosen domain must show that its constructed
positions satisfy both the structural conditions and the import restriction.
Any broader domain must be stated explicitly. In particular, a construction
using arbitrary board width needs an explicit generalization beyond this
engine's 8-by-8 limit. The movement and transition
rules can be formulated for arbitrary finite `X,Y`; the optimized implementation
is not an executable implementation of that entire generalized family.

The size of the encoded input must also be specified. A parameter such as
`max(X,Y,T,L)` does not by itself describe whether histories are stored explicitly
or succinctly, or how time coordinates are encoded.

This specification covers turn mechanics and the listed terminal conditions. It does
not add repetition, move-count draw rules, clocks, resignation, or draw agreements.

## Part III: Implementation correspondence and justification

### 12. Representation, proof, and sources

#### 12.1 The engine's stored and apparent values

The engine's `state::apparent_present()` returns the game concept in section 4:
the earliest active endpoint phase. Its `state::get_present()` instead returns
a stored phase used to retain the current player and a turn number while an
action is being composed. These are two implementation values, not two game
concepts called the Present.

At the start of a turn they agree. After a branching move, if the Present's turn
number is earlier than the stored turn number, the stored turn number is reduced
to it; the stored player stays the same. Otherwise the stored phase is unchanged.
Successful submission replaces the stored phase with the Present.

`state::get_timeline_status()` labels an active endpoint exactly at the stored
phase **mandatory**. Other endpoints with the current player's board-side are
**optional**. These labels describe the current partial action. Creating a
timeline can change activity and the Present, so they are not a fixed checklist
assigned at the turn's start. Section 8 defines submission independently of them.

`state::has_phantom_check()` also uses the stored phase when selecting endpoints
to skip. The check and mate definitions in section 9 apply at the start of a
turn, where it agrees with the Present. Applying that method to an arbitrary
partial action must not be confused with evaluating check for a new turn at the
current earliest endpoint.

#### 12.2 Movement rights and en passant history

A game position must retain the movement rights that affect future play, not
just the arrangement of pieces. A position notation may record castling
availability and en passant opportunities directly. The rules do not prescribe
an `umove` flag or any other storage layout. Arbitrary custom pawn placements
also need enough information to determine eligibility for a first double move;
castling and en passant information alone does not determine that eligibility.

This engine represents first-move rights for kings, rooks, pawns, and brawns
using an `umove` flag on each recorded piece. Movement clears it; copying a board
preserves it for the other pieces. Promotion does not create a fresh first-move
right. This represents the movement history and supplied rights in section 2.1.
En passant is different: its availability depends on the preceding move, so it
is not a persistent first-move right copied along with an uninvolved piece.

For en passant, the engine consults the current and preceding boards. In a legal
history this detects the immediately preceding two-rank first move. For a custom
history, the initial import checks do not prove that every recorded board follows
legally from the previous one, so the exact eligibility predicate is needed:

Let the capturing pawn or brawn be at `(x,y)`, let `f` be its forward rank
direction, and let `e` be either `-1` or `+1`:

1. The current board contains an enemy pawn or brawn at `(x+e,y)`.
2. Both `(x+e,y+f)` and `(x+e,y+2f)` are empty on the current board.
3. The immediately previous half-turn board on the same timeline exists and
   contains an enemy pawn or brawn at `(x+e,y+2f)` that is recorded as not having
   previously moved.

The move lands on `(x+e,y+f)` and removes the piece at `(x+e,y)` from the new
board. The implementation uses these board contents; it does not reconstruct
the historical move that connects supplied custom boards.

#### 12.3 Softmate: implementation and verification proof

Section 9 defines softmate without reference to an algorithm. The reviewed
change to `get_mate_type` implements that definition subject to the
assumptions below. The baseline commit identified in section 12.4 lacks the
reactivation filter and has an off-by-one bound in the arrival filter.

##### The two restrictions used by the implementation

The implementation first looks for a nonbranching legal action, then searches
branching actions subject to two restrictions:

1. A branch count must not activate an existing endpoint earlier than the
   original Present. Scan opponent inactive timelines in activation order and
   stop at the first earlier endpoint. If none is earlier, this imposes no cap.
2. An arrival creating an active new timeline must have destination turn number
   at least the original Present's turn number. Inactive new timelines are not
   subject to this restriction.

Finding either a nonbranching action or an action satisfying both restrictions
gives `NONE`. Otherwise, if some unrestricted legal action exists and the player
is in check, it reports `SOFTMATE`.

##### Claim and assumptions

**Claim.** At the start of a turn, among legal actions, the restricted branching
search admits exactly those branching actions whose resulting Present is later
than the original Present. Every legal nonbranching action also advances the
Present. Consequently, the complete classification procedure agrees with the
softmate definition above.

The proof uses the movement, activity, and submission rules in sections 3–8.
Its implementation correspondence assumes:

- The supplied state is at the start of a turn: its stored Present equals its
  earliest active endpoint phase. This claim does not cover arbitrary partial
  actions with different stored and apparent values.
- The underlying search is sound and complete for legal actions within the
  supplied search space, including the correspondence between branch axes and
  executable move sequences.
- If `legal_action_witness` is true, a legal action really has already been
  established for this state. This parameter is a caller-provided fact, not a
  substitute for one.

The argument proves the additional softmate restrictions correct. It does not
independently prove the correctness of the optimized legal-action search or of
the check detector. The classification uses the check predicate in section 9.

##### Proof: existing endpoints

Write the original Present as `P0 = (T,p)`, with `p = 0` for White and `p = 1`
for Black, so its phase index is `2T+p`. The player `p` stays fixed throughout
the action, even if the Present moves backward while the action is composed.

Every existing endpoint can advance at most once during the action. Only a
mover-sided endpoint can advance; its successor is opponent-sided and cannot
advance again. This also covers advancement by a nonbranching arrival.

Suppose an existing endpoint is initially earlier than `P0`:

- If it is opponent-sided, it cannot advance at all.
- If it is mover-sided, its phase index is at least two less than `2T+p`, because
  phases with the same board-side differ by an even number. Advancing it once
  still leaves it earlier than `P0`.

Thus an earlier existing endpoint remains earlier throughout this action. If
it becomes active, the final Present must be earlier than `P0`. Conversely,
an existing endpoint initially at or after `P0` cannot become earlier by
advancing. Existing active endpoints are all initially at or after `P0` by the
definition of the Present.

##### Proof: which existing timelines become active

Let `C` be the mover's number of created timelines and `O` the opponent's, counted
outward from the central timeline range. The activity rule allows the opponent
`min(O,C+1)` active created timelines initially. After the mover creates `k`
branches, this becomes `min(O,C+k+1)`.

Therefore, while opponent inactive timelines remain, each additional branch
activates exactly the next one, nearest the active boundary first. Existing
active timelines remain active. Increasing the mover's count does not activate
the mover's previously inactive timelines: their active allowance depends on
the unchanged opponent count.

If the first earlier endpoint follows `r` safe inactive endpoints, branch counts
`k > r` activate it, and counts `k <= r` do not activate any earlier endpoint.
The restriction is exactly `k <= r`, clipped to the maximum available branch
count. If all inactive endpoints are safe, there is no reactivation restriction.
Counting safe endpoints after the first unsafe endpoint would be incorrect;
activation cannot skip a timeline.

These counts apply to both odd and even layouts because they are measured from
the appropriate central range, rather than from the printed timeline labels.

##### Proof: new arrival endpoints and the half-turn advancement

A branch lands on a mover-sided board at a destination turn `t`. The new timeline
starts at the successor phase, with index `2t+p+1`. This endpoint is already
opponent-sided and cannot advance again during the current action. Its relation
to the original Present is therefore:

```text
2t+p+1 > 2T+p  if and only if  t >= T
2t+p+1 < 2T+p  if and only if  t < T
```

There is no equality case. This calculation explicitly includes Black's arrival
at `(t+1,White)` as well as White's arrival at `(t,Black)`.

The mover's `j`-th new timeline is active exactly when `C+j <= O+1`. Of the `k`
branches, precisely the first

```text
min(k, max(0, O-C+1))
```

are active in the final position. Only those arrivals need the restriction
`t >= T`. Endpoints of inactive new timelines do not contribute to the Present.

##### Proof: necessity, sufficiency, and classification

Consider a legal branching action. If it violates the reactivation restriction,
an earlier existing endpoint becomes active and remains earlier. If it violates
the arrival restriction, an active new endpoint is earlier. In either case the
final Present is earlier than `P0`. Both restrictions are therefore necessary
for a forward escape.

Conversely, suppose the legal action satisfies both restrictions. Each final
active endpoint belongs to one of three classes:

1. An originally active timeline: its endpoint began at or after `P0` and cannot
   move backward.
2. A newly activated existing timeline: the reactivation restriction ensures
   its endpoint began at or after `P0`, and it cannot move backward.
3. A new active timeline: the arrival restriction makes its endpoint strictly
   later than `P0`.

Every final active endpoint is consequently at or after `P0`, so their minimum
is at least `P0`. Successful submission requires the minimum to have the
opponent's board-side. It cannot equal the mover-sided phase `P0`, and must
therefore be strictly later. This proves sufficiency.

For a nonbranching legal action, activity does not change and only the first
class exists. The same minimum-and-board-side argument proves that every such
action advances the Present.

It follows that the zero-branch search together with the restricted branching
search finds an action exactly when a forward legal escape exists. If neither
finds one, every legal action rewinds the Present: equality is impossible after
submission. The unrestricted search, or a valid supplied witness, then
distinguishes having such escapes from having no legal action. Combining that
existence result with the check predicate gives precisely the chosen softmate
definition. This completes the proof under the stated assumptions.

##### Correspondence with the code and remaining validation

The relevant correspondence is inspectable in
[`state::get_mate_type`](../src/core/state.cpp) and
[`HC_info::build_HC`](../src/core/hypercuboid.cpp):

| Proof obligation | Implementation |
| --- | --- |
| Search the zero-branch case separately | `build_HC` orders the cases from the maximum branch count down to zero; `get_mate_type` removes and searches `ss.back()` |
| Restrict reactivation to the safe prefix | `max_reactivate` counts from the active boundary outward; the first endpoint `< present` sets `branch_cap`; no earlier endpoint leaves the cap unrestricted |
| Keep exactly the permitted branch counts | The loop over `ss` counts downward and copies only cases with `count <= branch_cap` into `ss2` |
| Filter only active new timelines | `active_branches = min(count, max(0, timeline_advantage+1))`, where `timeline_advantage = O-C`; the axis loop has an exclusive upper bound |
| Include the arrival half-turn correctly | The filter removes destinations with `am.m.to.t() < present.t()`, exactly the condition proved above |
| Preserve legal escapes into the past for classification | `ss` remains unrestricted for the later existence search; only `ss2` receives the forward-escape restrictions |

The change adds the reactivation restriction and fixes the previous
off-by-one axis bound. The maintainer has reviewed and accepted this implementation,
including the existing descending iteration over branch-count cases. It is included
in main-branch commit `80f1588a61361340c151b38f2eef6f9543e5216b`. The proof above is a mathematical
argument checked against these source operations, not a machine-checked proof.

For the initial implementation change, the build succeeded and eight of nine
selected existing checks passed. `hc_count_fine` failed on a duplicate-sibling
assertion that also reproduced in an isolated build of the unchanged baseline.
Those are historical validation results, not a claim about later edits to the
fine-tree code. An additional enumeration checked the activity-count formulas
in 5,328 synthetic cases across both layouts and players; it did not exercise
complete softmate positions. Neither these checks nor generated test expectations
replace the proof's assumptions about the underlying search.

Dedicated softmate regression cases remain deferred at the maintainer's request
until the specification details are settled. Softmate is not needed for the
definitions of checkmate and stalemate or for a proof about the existence of a
legal action.

#### 12.4 Source authority and verification status

The initial source review used baseline commit
`2c4e353789c7b136119ae8542c0cf9590ddacaed`. The maintainer reviewed and accepted
the softmate change described in section 12.3, including its current search-space
iteration direction, during this discussion on 29 October 2026. That change is
now included in `main` at commit `80f1588a61361340c151b38f2eef6f9543e5216b`,
which also contains a separate fine-tree change. The proof retains its explicit
assumptions about the underlying search.

The primary evidence is **the implementation in `src/core` that has been merged
into `main`**. The maintainer reviews that code before it is merged. This review
status does not automatically extend to an unmerged branch or to tests.
In particular, `test/test_movegen.cpp` was generated by Codex and has only been
partially reviewed. Its expectations are supplementary evidence, not a source of
rules and not an independent confirmation of the rules below.

This specification is a source-based account, not a formal verification of the engine.
Descriptions derived from code can still contain interpretation errors. Where
the intended rule or a proposed simplification needs confirmation, the document
says so. Agreement with the commercial game's behavior is a separate question.

The following links are evidence for the specification. Symbol names identify the relevant
parts without relying on line numbers that will drift as files change.

| Subject | Source and relevant symbols |
| --- | --- |
| Board phases | [`turn.h`](../src/core/turn.h): `turn`, `next_turn`, `previous_turn` |
| Timeline labels and reflection | [`timeline_layout.h`](../src/core/timeline_layout.h): `mirror`, `initial_range`, `pretty_l` |
| Board-size bounds and piece identities | [`piece.h`](../src/core/piece.h) |
| Boards, movement, activity, apparent Present | [`multiverse.cpp`](../src/core/multiverse.cpp): constructor, `get_present`, `calculate_active_range`, move generators |
| Physical attack and castling support | [`board.cpp`](../src/core/board.cpp): `attacks_to`, `is_under_attack`, `castling_rooks`, `move_piece` |
| Shared temporal directions | [`move_geometry.h`](../src/core/move_geometry.h) |
| Movement, branching, stored Present, submission | [`state.cpp`](../src/core/state.cpp): `apply_move`, `submit`, `test_present`, `get_timeline_status` |
| Promotion settings | [`state.h`](../src/core/state.h), [`variants.h`](../src/core/variants.h), [`promotion_header.cpp`](../src/core/promotion_header.cpp) |
| Royal captures and hypothetical skipping | [`check_position.cpp`](../src/core/check_position.cpp): `for_present`, `for_phantom`, `scan`, `first_check`; [`state.cpp`](../src/core/state.cpp): `phantom`, `has_phantom_check` |
| Mate and softmate classification | [`state.cpp`](../src/core/state.cpp): `get_mate_type` |
| Optimized action search | [`hypercuboid.cpp`](../src/core/hypercuboid.cpp), [`hypercuboid.inl`](../src/core/hypercuboid.inl) |
| Checked setup and action loading | [`pgn_loading.cpp`](../src/core/pgn_loading.cpp), [`state.cpp`](../src/core/state.cpp): `from_ast`, `find_royal_exposure` |
| Action representation and printing | [`action.cpp`](../src/core/action.cpp), [`semimove.cpp`](../src/core/semimove.cpp) |

The baseline descriptions in this specification were checked by reading the reviewed
core sources. Section 12.3 separately identifies the reviewed softmate change and
proves the equivalence of its restrictions to the chosen definition, conditional
on correct legal-action search. This work does not independently prove that every
optimized search path returns exactly the legal actions defined in section 8.
A future simple reference implementation could help check that correspondence.

The initial draft changed no engine behavior; the subsequent softmate
change is recorded in section 12.3. The worked examples are currently
reasoned examples; they are not recorded runtime measurements. Tests, including
the partially reviewed `test_movegen`, must not be cited as independent authority
for an expectation merely because they encode that same expectation.

### 13. Review status and remaining validation

The rule and scope decisions below are settled. The maintainer has reviewed and
accepted the softmate implementation. On 30 October 2026, the maintainer also
completed a read-through of Part I and the semantics in Part II and approved
placing this document in `docs/`. This does not imply formal verification of
Part III or completion of the validation work listed below.

#### Settled decisions

- **Castling:** the maintainer confirmed that the same-board attack test described
  in section 5.6 is intentional, including the custom-position behavior. It is
  not an open rule question.
- **Softmate:** the chosen definition is check with at least one legal action,
  where every legal action moves the Present backward. Section 9 states the
  definition; section 12.3 contains the completed equivalence argument for the
  updated filters, conditional on correct
  underlying legal-action search and the stated calling assumptions. The
  implementation, including its iteration direction, has been accepted. This
  definition need not cover every community use of the term.
- **Input domain:** this specification concerns structurally valid custom
  positions satisfying the checked import restriction: neither color may have
  an immediate capture of an opposing royal piece. Reachability from an opening
  is not required. The capture test is the endpoint-based test in section 7,
  without hypothetical skipping or a requirement to complete a legal action.
  `UNSAFE` imports can admit a broader class of positions and are outside this
  chosen domain.
- **Terminology:** use **the Present** for the game concept, meaning the earliest
  active endpoint phase. Discuss stored and apparent values only when explaining
  this implementation. Keep historical boards distinct from opponent-sided
  endpoints that the current player cannot advance.

#### Remaining work

1. **Implementation correspondence and proof:** Part III states the evidence
   and assumptions supporting the rules. The completed gameplay and semantics
   read-through does not establish correctness of the entire optimized search.
2. **Deferred validation:** dedicated softmate regression cases and selection of
   manually reviewed reference examples remain deferred. Expected outcomes should
   follow from reviewed rules, not simply be copied from generated tests.
3. **Complexity theorem scope:** any theorem using these states must explicitly
   state its board-size generalization and input encoding, and show that its
   constructed positions satisfy the chosen import restriction. These are proof
   obligations, not unresolved mechanics rules.

The softmate proof remains conditional on the underlying legal-action search;
this document does not formally verify the entire optimized search algorithm.

This is the permanent home of the mechanics specification. The separate
[`Branched 5DPGN`](branched-5dpgn.md) document describes notation and has not been
changed as part of this publication.

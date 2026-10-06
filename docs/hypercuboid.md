The Hypercuboid Algorithm
=========================

Search for legal actions
----------------------

Searching for legal action for a given state occurs ubiquitously. For example, it is needed to determine whether the game state is checkmate, to generate possible candidate for a chess engine, etc.

One may propose this naïve approach:
```lua
function search(state) -> action:
    action = []
    for all boards where player can move:
        for all legal moves:
            action.append(move)
        new_state = apply_action(action)
        if opponent does not check the player's king:
            if present is moved:
                return action
    return nil
```
This approach is not very efficient. Suppose one board has 20 legal moves (which is the case for the chess opening), and there are 10 timelines. Searching through all possible actions would require 20^10 = 1.024*10^13 iterations. In fact a heruistic formula for the number of legal actions is 40^n, where n is the number of timelines. 

If the situation is checkmate, then the computer must search through all possible actions to confirm the checkmate. This is not plausible in a reasonable time frame. 

Hypercuboid Algorithm: An Introduction
------------------------------

The Hypercuboid algorithm is designed to address the time complexity issue. Its behavior is similar to a SAT solver which operates on positive information.

A *hypercuboid* is a N-dimensional cube, whose points represent candidates for legal actions. The algorithm starts from a full hypercuboid, which is known as the *universe*. It sets the universe as the initial search space, and then iteratively
* find a point inside
* check if the point is a legal action
    * if it is legal, return the point
    * otherwise, remove the point (and possibly other points) from the hypercuboid and repeat

In pseudocode, it is
```lua
function hypercuboid_search(state) -> action:
    search_space = build_universe(state)
    while search_space is not empty:
        hc = last hypercuboid in search_space
        point = take_point(hc)
        problem = find_problem(point, hc):
        if problem is nil:
            return to_action(point)
        else:
            remove the point and other points 
            that have the same problem from 
            the search_space
    return nil
```

Moves vs. Semimoves
-------------------
Tradiationally, an action is represented as a sequence of move. Note it must be a sequence instead of a set since order matters. 

The hypercuboid algorithm, however, uses a different representation of an action that is more friendly to be handled geometrically. Say we have n timelines for the current state. After an action is performed, we will have at most 2n timelines, which is the case when all moves in the action are branching. The hypercuboid algorithm represents an action as a 2n-sequence of *semimoves*, which is what performed on each (existing or newly created timeline). These are the possibilities:

* Physical move: a piece moves within this board
* Departure: a piece leaves this board 
* Arrival: a piece lands on this board
* No-op: nothing happens

We will code these situations as physical_semimove, departure_semimove, arrival_semimove, and null_semimove respectively.

Note: for existing timelines, the semimove on them can be virtually anything. For newly created timelines, the semimove must be either arrival_semimove or null_semimove.

Representing the Search Space
-----------------------------

A hypercuboid will be represented by a 2n-list of sets, where each set contains the possible semimoves for the corresponding timeline.

(Optimization hint: 2n can be optimized to n+k where k is the maximal number of possible new timelines that can be created in this turn. Or, k can be taken as the number of playble timelines that has a branching move departing from it.)

We work on *search spaces*, which are subsets of the universe. Sometimes the search space looks like a hypercuboid, so we will represent it in the way just described. Otherwise, we will represent it as a list of hypercuboids, meaning that the search space is the disjoint union of all the listed hypercuboids.

The problem to be removed are represented as *slices*, which are hypercuboids with some axes with a few semimoves, while other axes with all allowed semimoves.

### Example

Suppose a hypercuboid is has 3 axes, where each axis have the following semimoves:
* Axis 0: {a, b, c}
* Axis 1: {d, e}
* Axis 2: {f, g, h}
The hypercuboid is in principle a 3-dimensional cube, with 3*2*3 = 18 points. However, in practice, we only need to store a dictionary (or list) of the axes and their semimoves, which is
```
{
    0: {a, b, c},
    1: {d, e},
    2: {f, g, h}
}
```

And we have a problem slice that is
* on Axis 2: {g, h}
* on other axes: all semimoves

This problem slice can be read as: banish semimoves g and h on axis 2.

The problem slice is a plane (with thickness 2) in the 3-dimensional space. Then removing the problem slice from the hypercuboid is equivalent to removing all points that has g or h on axis 2. The resulting hypercuboid is
```
{
    0: {a, b, c},
    1: {d, e},
    2: {f}
}
```

On the other hand, if the problem slice is
* on Axis 0: {a}
* on Axis 1: {d}
* on Axis 2: all semimovess

This problem slice can be read as: banish this combination: on axis 0, semimove a; on axis 1, semimove d. In other words, a and d cannot both in the remaining search space after removing this problem slice.

The problem slice is a line. After removing it, the resulting space is no longer a hypercuboid. We will represent it as a list of two disjoint hypercuboids (a search space):
```
[
    {
        0: {b, c},
        1: {d, e},
        2: {f, g, h}
    },
    {
        0: {a},
        1: {e},
        2: {f, g, h}
    }
]
```
(Think it as separating a plane from the cube first, then remove the line from the plane, and put the remaining two pieces together.)

Building the Universe
----------------------

### If l is an existing timeline
The semimoves on l is the collection of
* exactly one null_semimove
* all physical moves on l, recorded as a physical_semimove
* all moves that leaves l, where for each move, we remove the information on the location where the piece lands, and only record a departure_semimove
* all moves that arrives on l at the Present, where all the infomation, including the departure location is retained in the arrival_semive

### If l is a newly created timeline
The semimoves are not reliant on l. They are always the collection of
* exactly one null_semimove
* all moves that arrives on any timeline (for whatever turn it arrives on), recorded as an arrival_semimove with full information

(Optimization hint: we can build the universe without adding the semimoves that place the player's royal piece in physical check)

Converting a Point to an Action
-----------------------------

To convert, read the semimoves in perspective of the current player, from the axis of the oldest timeline to the axis of the newest timeline. If the semimove on the axis is a physical semimove or an arrival semimove, append it to the sequence of moves. Otherwise, skip it. The resulting sequence of moves is the action.

Note that in this design departure_semimoves are merely used as supplementary information to help find a legal action. They are not needed in the conversion process.

Take a Point
-----------------

Since the search space is a list of hypercuboids, we can just take a point from the first hypercuboid in the list. But we can actually do something more. A point, as a 2n-sequence of semimoves, does not guarantee that the semimoves are compatible with each other. For example, if it contains a departure_semimove and no arrival_semimove anywhere, then we cannot find a action that correspond to exactly this point.

We use a graph matching algorithm to prevent this situation.

The graph to be built is
* An undirected graph with 2n vertices
* There exists an edge between vertices v0 and v1 if and only if either
    + there exists a departure_semimove on axis v0 and an arrival_semimove on axis v1, where the departure and arrival match each other; or
    + there exists a departure_semimove on axis v1 and an arrival_semimove on axis v0, where the departure and arrival match each other

In the meanwhile, we also build a set must_include, which contains all vertices (axes) that have no physical_semimove or null_semimove on them. This means in the found point, if any, the semimove on that axis must be related to certain semimove on another axis.

By a matching we mean a set of edges where
+ no two edges share a common vertex
+ every vertex in must_include is included in some edge in the matching

```lua
function take_point(hc) -> point:
    G = a disconnected undirected graph with 2n vertices
    must_include = []
    -- each vertex correspond to an axis
    for each axis in hc:
        has_nonjump = false
        for each semimove in axis:
            if semimove is physical_semimove:
                has_nonjump = true
            else if semimove is arrival_semimove:
                add an edge in G between this axis and the
                axis of the departure timeline
        if not has_nonjump:
            add this axis to must_include
    matching = find a matching in G that includes all vertices
               in must_include
    if matching is nil:
        return nil
    point = empty 2n-sequence
    for v0, v1 in matching:
        let point[v0] and point[v1] be a pair of compatible jump in
        hc[v0] and hc[v1] respectively that departure matches arrival
    for each axis not in matching:
        let point[axis] be a physical_semimove or null_semimove in hc[axis]
    return point
```

(Optimization hint: since we only use arrive_semimoves to code actions and find problems, after a while, it is possible that certain departure_semimoves are dangling, meaning that there is no arrival_semimove that matches them. In this case, we can remove those departure_semimoves from the hypercuboid.)

The method to find a matching is described in [docs/graph-matching.md](graph-matching.md).

Find a Problem
-----------------

Besides depature may not match arrival, there three more types of problems that may occur for a point. We resolve them in the following order. This assumes, in the second step, the problem of in the first step has been resolved, and in the third step, the problems of the first two steps have been resolved.

1. Although arrives matches departure, the jump order is still not consistent. Resolving this will make sure the point can be converted into a sequence of moves.
2. The sequence of moves converted from the point does not move the Present.
3. The sequence of moves converted from the point is illegal because if applied, the opponent will check the player's king (or royal queen).

If a problem is found, we will not only remove the point, but also remove other points with the same problem. Such points will be in the form of a slice.

```lua
function find_problem(point, hc) -> slice:
    if jump_order_consistent(point, hc) fails:
        return the problem slice
    if test_present(point, hc) fails:
        return the problem slice
    if find_checks(point, hc) fails:
        return the problem slice
    return nil
```

### Jump Order Consistency

In a branching jump `(0T5)Qb3>>x(0T1)f7+`, the queen departures from `(0T5)b3` and arrives at `(0T1)f7`. It then creates a new timeline, thus stays on `(1T1)f7`. We shall distinguish the last two locations and only call `(0T1)f7` as the arrival location for that move.

There are two cases:
1. A jump arrives on a playable board (l,t), but the semimove on that board is a null_semimove.
    + Problem slice: product of
        - on axis for l, the null_semimove
        - on new axes, all semimoves that arrives on that same playable board
2. there is a branching move that depaetures from the board (l,t) and stays on new_l, and there was another jump that arrives on (l,t) but stays on new_l0 where new_l0 happens latter than new_l
    + Problem slice:
        - on axis for l, any move starts from (l,t)
        - on axis for new_l0, any move goes to (l,t)

### Test Present

This is done alongside simulating the movement of the Present. Start from the current Present, we read axis inside the newly created timelines in the order of their creation. The Present can shift back to the past in two cases:

1. An active new timeline is created earlier.
2. After a jump, an existing (inactive) timeline is reactivated.

After we located the Prsent, we check if the semimoves on active boards on that turn are sufficient to move the Prensent. If not, that is, there is a null_semimove on certain timeline l where the last board is on turn t, which is exactly the new Present, then we have a problem. The problem slice is the product of
* on axis for l, the null_semimove
* on axes for newly created timelines, all moves that doesn't create an *active* branch before the new Prensent

### Find Checks

For this we construct the state after the action is applied, and check if the opponent checks the player's royal piece. If so, record the path of the check, if the check is created by a sliding piece. The problem slice is the product of
* on the axis for the player's royal piece, the semimove that put player's royal piece there
* on the axis for the opponent's piece that checks the player's royal piece, the semimove that put opponent's piece there
* on the axes for the timelines where the checking piece crosses, the semimoves that do not block the checking path

(Optimization hint: on the axis for the player's royal piece, placing another king there also don't help. So those semimoves can be removed. On the axes for the opponent's checking piece, allowing opponent to put another sliding piece that can slide through the same checking path also don't help. So those semimoves can be removed as well. On the axes for the timelines where the checking piece crosses, it doesn't work if the we block the checking path with a king or the same opponent's piece. So those semimoves can be removed as well.)

Revisiting the Hypercuboid Search
---------------------------------

In the pseudocode program above, we remove problem slices inside the last hypercuboid in the search space, i.e. where the problematic point is taken from. In principle, we can remove any slices that

1. contains the problematic point; and
2. only remove points that are invalid. 

The first property ensures the termination of the algorithm, since in each iteration the volume of the search space is reduced. Once we prove the points passes all tests are legal, the second property guards the correctness of the algorithm, since we only remove points that are invalid.

Moreover, when we found a problem slice, we can remove it from all hypercuboids in the search space, not just the last one. However, there is a trade-off:
* each removal of a slice with codimension in the hypercuboid larger than 1 with create more than one hypercuboid in the search space, which may fragment the search space, making future search slower;
* if we only remove the slice from the last hypercuboid, we may rediscover the same problem slice again and again in future iterations.

Empirically, an effective strategy is to start from the last hypercuboid, remove the problem slice from it and then from the previous hypercuboids, until we reach a hypercuboid that does not intersect with the problem slice.

Another strategy, which is probably better, is to remove the problem slice from the last hypercuboid, and only remove the problem slice from the previous hypercuboids if the codimension of the problem slice is 1. We stop trying if the non-intersecting hypercuboid reaches 10% of all the hypercuboids visited. This way, we avoid fragmenting the search space too much, while still removing some invalid points from previous hypercuboids.

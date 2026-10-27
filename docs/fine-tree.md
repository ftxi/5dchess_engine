The Fine-Tree Architecture
=================

A fine-tree is defined with two main components: `fine-node` and `fine-cell`.
- `fine-node` owns the search tree of semimoves (for external applications, e.g. MCTS). It may own more additional data: `fine-node<T>`.
- `fine-cell` owns the tree of hypercuboids.

Goal
----

This refactor aims to make `fine-tree` adoptable with the optimizations implemented recently in `HC_info::search()`.

In `fine-tree`, we want it to be able to not only generate all legal actions, but also in an order as close to the given HCOrdering as possible.

The redesign does not implement multi-threading yet, but we want it ready to do that.

API
-------
fin
Only `fine-node` needs to be visible from outside. Suppose `node` is an object of `fine-node`.
+ `node.search()` is a generator that yield all child fine-node's pointers. We may actually provide `node.search<Order, Policy>()`
+ `is_ceiling`
+ `ignite`

In the rest of this artical "relative-" means we only consider the sub-fine-tree starting from the cloest nodal ancestor: e.g. 
* relative-root (meaning that nodal ancestor)
* relatice-ancestor
* relative-path

Architecture
------------

A sepecial kind of fine-node is the nodal `fine-node`. A nodal `fine-node` represent a clean state: either initial or immediately after submit; thus there is no pending move to be submitted later. It owns the `HC_info` and all the `fine-cell`s that are responsible for the search for this state.

A normal `fine-node` represents a semimove. The relative-path towards it means the semimoves to construct an (maybe not yet complete-)action. 

Each `fine-node` shall attach to multiple `fine-cell`s. Linking `fine-cell` means that the cell is relevent to searching further down the path of the current `fine-node`. A `fine-cell` need not only contain possibilities only in that path; multiple `fine-node` can link to the same `fine-cell`.

`node.remaining` is a HC which in principle contains the area for possible extension. If we stipulate each fine-tree level represent a certain level, then `node.remaining` is
- on the axis `n` of node's semimove or an relative-ancestor's semimove: `node.remaining[n]` is the singleton containing that unique semimove
- on the axis `n` for its children: `node.remaining[n]` is all possible coordinates excluding the chidren
- Splitting a cell must preserve every candidate not excluded by a sound constraint.

Each `fine-cell` contains one `HC` and possibly multiple slices as negative information.

Contracts
---------

During expansion, these contracts must hold:
+ No two `fine-node`s represent the same semimove.
+ In the cell tree, children `fine-cell` of a single `fine-cell` must contain mutually exclusive HCs.
+ If `c2` is a child cell of `c1`, then `c2.hc` is a subset of `c1.hc`.

These are maintained contracts which can break temproarily:

+ ~~A `fine-node` should link to leaf `fine-cell`s~~
+ A `fine-node` should not link to `fine-cell`s that cannot be expanded anymore

Ingnition
---------

For fine-tree, I recommend these changes in build_HC (we can declare another function, e.g. name it build_solid_HC)
1. In original build_HC, the universe is split into slices which are 0 new timeline, 1 new timeline, etc. In build_solid_HC, we don't do this, but rather record the removed part as a problem slice in negative information
2. Besides removing the physical checks, we use check_position to remove the jumps that create checks as well.

Procedure
---------

Search occurs in `fine-node`s. `node.search()` does these:

1. Iterate all cells it links to (call it `c`)
2. Run DFS to visit leaf cells ¿in which order?
    - If `node.remaing` intersects `leaf.hc`
        - take a point in the intersection, update `node.remaining` and then co_yield it, set the YIELDED flag as true
        - go to the local search mode if the local hc intersects remaining
    - Otherwise, mark this leaf cell as not helpful
        - mark the parent cell as not helpful if all children as not helpful
        - if all but one children of is marked as not helpful, then re-link `c` to the only helpful cell
3. After iterated all cells, co_return if YIELDED is false, otherwise go back to step 1



The Graph Matching Algorithm
============================

We are given an undirected graph G = (V, E) and a set of vertices S, where each vertex is connected to at least one other vertex in G. The goal is to find a matching M that includes all vertices in S, or return nil when no such matching exists.

By a matching we mean a set of edges where
+ no two edges share a common vertex
+ every vertex in S is included in some edge in the matching

Augmenting Paths
----------------

The matching algorithm is done by starting with an empty matching M, repetitively finding augmenting paths in the graph and improve along them. By an augmenting path we mean a path that

- start at a vertex which is in S but not in M; and
- alternating edges are inside M; and
- if the path has odd length, it ends at a vertex which is not in S.

To improve along an augmenting path, we update M to be the symmetric difference of M and the edges in the augmenting path.

For example, consider the following graph:

```
  A===B
  |   |
  C---D
```

Where `A===B` means it is an edge in the matching M, while `A---C---D---B` are not in M. Then `C---A===B---D` is an augmenting path. To improve along this path, we will add `C===A` and `B===D` to M, but remove `A===B` from M. The new matching will be:

```
  A---B
 ||   ||
  C---D
```

The Algorithm
-------------

```lua
function find_matching(G, S) -> matching
    M = []
    while some vertex in S is not matched in M:
        P = find_augmenting_path(G, M, S, vertex)
        if P is None:
            return nil -- cannot improve anymore
            -- while no matching exists
        M = symmetric_difference(M, P)
    return M
```

### Termination

There are two types of augmenting paths:

* Case 1: the augmenting path has even length, i.e. it starts and ends with unmatched vertices. In this case, improving along it increases the matching size by one.
* Case 2: the augmenting path has odd length, i.e. it starts with an unmatched vertex and ends with a matched vertex. In this case, by inproving along it, the matching contains one more vertex from S.

Thus we conclude that the algorithm stops in at most |V|+|S| iterations.

### Correctness

Assuming the correctness of finding augmenting paths, we can prove the correctness of the algorithm by contradiction.

Suppose the algorithm returns nil while there exists a matching M' that includes all vertices in S. Then there is a vertex u in S that is not matched in M. 

1. By assumption, u is in S. Because M' is a matching that includes all vertices in S, there must be an edge in M' that connects u to some vertex v. If v is not in M, then u---v is already an augmenting path. 

2. Otherwise, v is in M. This means v is matched to some vertex w. Note that w cannot be the same as u, because w is matched with v in M while u is not. If w is not in S, then u---v===w is an augmenting path.

3. Otherwise, w is in S. Go back to step 1 with w in place of u.

Note that we cannot have an infinite loop. A loop while create a minimal cycle in G where all vertices u ... x --- y === z ... t are distinct and y is the first vertex that occurs twice. So x and t are not the same vertex, and they cannot match to the same vertex in M'.

```
u ... x --- y === z
            |     |
            t ... .
```

Because the graph is finite, this process must terminate with an augmenting path, which is the desired contradiction.

Finding Augmenting Paths
------------------------

To find the augmenting path, we start from a vertex u in S that is not matched yet, and perform a breadth-first search along G. A basic idea is to search with the desired alternating pattern, record the visited vertices and never visit a visted vertex again. However, this might be dangerous because there may be a node both reachable by odd-length or even-length alternating paths. To handle this situation, we use the [Edmond's blossom search](https://en.wikipedia.org/wiki/Blossom_algorithm).

The idea is: whenever we found a vertex v that is both odd reachable and even reachable, the two paths form an odd-length cycle, which we call it a blossom. We may mark any vertex inside the blossom as even reachable, and continue the search with the blossom contracted.

```lua
function find_augmenting_path(G, M, S, root) -> path
    -- initialization
    for each vertex v in G:
        v.mate = the vertex that v is matched to in M, or nil
        v.parent = nil -- the parent in the BFS search tree 
        v.in_current_blossom = false
        v.blossom_base = v
        if v is root:
            v.is_even_reachable = true
        else:
            v.is_even_reachable = false
    queue = [root] -- only even reachable vertices queued
    -- search
    while queue is nonempty:
        v = queue.pop()
        for each neighbor u of v in G:


```
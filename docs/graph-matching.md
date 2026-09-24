The Graph Matching Algorithm
============================

We are given an undirected graph G = (V, E) and a set of vertices S, where each vertex is connected to at least one other vertex in G. The goal is to find a matching M that includes all vertices in S, or return nil when no such matching exists.

By a matching we mean a set of edges where
+ no two edges share a common vertex
+ every vertex in S is included in some edge in the matching

Augmenting Paths
----------------

The matching algorithm is done by starting with an empty matching M, repetitively finding augmenting paths in the graph and improve along them. By an augmenting path we mean a path that

- start at a vertex which is in S but not in M (the zeroth vertex); and
- alternating edges are inside M; and
- if the path has even length, it ends at a vertex which is not in S.
- if the path has odd length, it ends at a vertex which is not in M.

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

The idea is: whenever we found a vertex v that is both odd reachable and even reachable, the two paths form an odd-length cycle, which we call it a blossom. We can define a new graph G' by contracting the blossom into a single vertex, and recursively search for an augmenting path in G'. If we find an augmenting path in G', we can lift it back to G by expanding the blossom.

```lua

function find_augmenting_path(G, M, S, root) -> path
    -- initialization
    for each vertex v in G:
        v.parent = nil -- the parent in the BFS search tree
        if v is root:
            v.is_even_reachable = true
        else:
            v.is_even_reachable = false
    queue = [root] -- only even reachable vertices are queued
    while queue is nonempty:
        v = queue.pop()
        for each neighbor u of v in G:
            if u.is_even_reachable is false:
                -- normal behavior: u is only odd reachable
                if u.parent is not nil:
                    -- u is already visited, but it is odd reachable
                    continue
                u.parent = v
                if u is not matched in M:
                    -- found an odd length augmenting path
                    return bfs_tree_path(root, u)
                else:
                    w = the vertex that u is matched to in M
                    w.parent = u
                    if w is not in S:
                        -- found an even length augmenting path
                        return bfs_tree_path(root, w)
                    else:
                        w.is_even_reachable = true
                        queue.push(w)
            else:
                -- found a blossom
                blossom.base = the closest common ancestor of v and u in the BFS tree
                blossom.v1 = v
                blossom.v2 = u
                if there exists a vertex in blossom.vertices that is not in S:
                    -- found an even length augmenting path
                    return concatenation of
                        bfs_tree_path(root, blossom.base),
                        even_arc(blossom, the vertex not in S)
                else:
                    -- contract the blossom and search recursively
                    G’, M’, S’ = G, M, S after contracting the blossom into a single vertex
                    b’ = the contracted vertex in G’
                    P’ = find_augmenting_path(G’, M’, S’, root)
                    if P’ is nil:
                        return nil
                    else:
                        -- lift the path
                        if b’ is not in P’:
                            return P’
                        else:
                            x = the original endpoint inside blossom
                                of the P’s unmatched boundary edge
                            arc = even_arc(blossom, x)
                            -- although blossom.base is always reached by an even length path
                            -- in find_augmenting_path(G’, M’, S’, root), b’ may be reached by
                            -- an odd length path in P’, so we may need to reverse the arc
                            -- this is not necessary if the subprocess reuses existing BFS tree
                            if b’ is not root and the edge (b’.parent in G’, b’) is not in M’:
                                arc = arc reversed
                            return P’ with the contracted vertex replaced by arc
    -- BFS queue is exhausted whereas no augmenting path is found
    return nil

function bfs_tree_path(root, v) -> path
    path = []
    while v is not root:
        path.push(v)
        v = v.parent
    return path reversed

function even_arc(blossom, v) -> path
    if v.is_even_reachable:
        return bfs_tree_path(blossom.base, v)
    else:
        -- trace the longer arc inside the blossom
        if v is on the path from blossom.v1 to base:
            return concatenation of
                bfs_tree_path(v, blossom.v1) reversed,
                blossom.v1-blossom.v2,
                bfs_tree_path(blossom.v2, base)
        else:
            return concatenation of
                bfs_tree_path(v, blossom.v2) reversed,
                blossom.v2-blossom.v1,
                bfs_tree_path(blossom.v1, base)

```

To contract a blossom:
- For G, replace all vertices in the blossom with a single vertex b’, and replace all edges that connect to the blossom with edges that connect to b’. Remove all edges that are inside the blossom and leave other edges unchanged.
- For M, apply the same transformation as G: Remove all edges that are inside the blossom and leave other edges unchanged. Note that there is at most one edge in blossom that can be matched to the outside:
    - if base is root, then there is no matched edge connecting to the outside.
    - otherwise, there is exactly one matched edge connecting to the outside, which is the edge connecting base and its parent. Reason: since blossom.v1 and blossom.v2 are both even reachable, they and their encestors in the blossom are all matched to internal vertices. 
- For S, note that we have checked all vertices in the blossom are in S' before contracting. Set S' = b’ and all vertices in S but not in the blossom.

Therefore, M' remains to be a matching in G', and S' remains to be a subset of vertices in G'.

### Termination

Each time we find a blossom, the contraction reduces the number of vertices in the graph. And the BFS search on a graph G=(V, E) will terminate in at most |V| iterations. Thus the algorithm will terminate in O(|V|^2) queue operations.

### Correctness

There are two parts:
- **Soundness:** All paths returned by the algorithm are indeed augmenting paths.
- **Completeness:** If there exists an augmenting path, the algorithm will find one.

*Proof of soundness.*

Suppose the algorithm returns an augmenting path P. Prove by induction on the number of contractions.
+ If there is no contraction, then the algorithm is a standard BFS search on the pattern of alternating edges, and stops accordingly when the length is even or odd.
+ Assuming the soundness of the algorithm with no more than n-1 many contractions, we justify the soundness of the algorithm with n contractions.
    - By induction hypothesis, the algorithm is correct on the contracted graph G'. The produced P' is an legitimate augmenting path in G'.
    - Since b' is in S', P' cannot be a even length path ending at the contracted vertex.
    - If b' is the root of G', then P' cannot be just one vertex b'. If b' is not the root of G', then b' is matched to something in M'. Therefore, in either cases P' cannot be an odd length path ending at the contracted vertex.
    - The lifting procedure ensures that arc is a even length path connecting base to w with correct alternating M-edges pattern. Moreover, we have shown the b' is not an endpoint of P'. So the lifting also preserves the endpoint behavior.

*Q.E.D. (Soundness)*

We abstract the pattern of ablossom general. A blossom is an odd-length cycle
+ with a designated vertex called the base; and
+ the two vertices adjacent to the base are not matched to the base, but the walk
```
base---v1===v2---...===vn---base
```
form an alternating pattern of edges in M and not in M; and
+ all vertices in the blossom pattern should be in S.

**Lemma 1.** Suppose G' is obtained from G by contracting a blossom B and there exists an augmenting path P in G. Then there exists an augmenting path P' in G'.

Suppose there exists an augmenting path P in G. If B is a blossom, the contracted path P' defined as follows:
+ If P does not intersect with B, then P' = P.
+ Otherwise, let P' be the path obtained by replacing the subpath of P that starts at the first vertex in B and ends at the last vertex in B with the contracted vertex b'.

We will show that P' is an augmenting path in G'.
+ If P does not intersect with B, then there is nothing to prove.
+ If P intersects B, then P must enter B at some vertex v1 and exit B at some vertex v2.
    - Suppose P enters B at the base. Then the first vertex in the intersection must be the base, or a matched vertex. With the same reasoning in the proof of soundness, P cannot terminate inside B. 
    - Suppose P enters B at a vertex v1 that is not the base. Then P enters through an unmatched edge, because the only possible edge that is matched to the outside is the base. For the same reason, P must exit B through base.
+ Note that P crosses B at most once because each time it crosses it crosses the base. In either case, P' is obtained from P by removing even number of edges and remains to be an augmenting path.

*Q.E.D. (Lemma 1)*

**Lemma 2.** (1) If the algorithm returns nil without contracting any blossoms, then there is no augmenting path in G starting from the root.
(2) If the algorithm returns nil on G', then there is no augmenting path in G starting from the root.

*Proof of Lemma 2.*

(1) Suppose there is an augmenting path P in G. Let's say P is v0=root, v1, v2, ..., vn. The root is added to the queue, and then marked as even reachable. We claim: each of v0, v2, ..., will be maked as even reachable and enqueued, whereas each of v1, v3, ..., if visited, will be marked visited (by setting the parent pointer) but not marked as even reachable or enqueued.

Let's say v{2k} is poped out from the queue. For inductive prove, we need to show the claim for v{2k+1} and v{2k+2}. If v{2k+1} is already marked as even reachable, the algorithm will either proceed to contract a blossom or return an augmenting path, both forbidden by the assumption. In the next branch of the algorithm, if v{2k+1} is marked as visited, fine. Otherwise, v{2k+1} is not visited yet. The algorithm will check if v{2k+1} is matched in M, but the answer shall be yes because otherwise an augmenting path would be the return value instead of nil. With v{2k+1} matched, the algorithm will mark it as visited and set its parent to be v{2k}. 

Because the edge from v{2k+1} to v{2k+2} is the {2k+2}-th edge in P, it must be in M. And this very edge is the unique mate of v{2k+1} in M. Therefore, after the first time seeing v{2k+1}, the algorithm checks if v{2k+2} is in S (answer: yes, by assumption on return value), then mark it as even reachable and adds v{2k+2} to the queue.

Therefore, the algorithm will run into the last vertex vn. At this point, one of the termination conditions necessarily holds, giving the desired contradiction.

(2) Suppose there is an augmenting path P in G. By Lemma 1, there exists an augmenting path P' in G'. The proof concludes with induction on the number of contractions, where the base case is (1).

*Q.E.D. (Lemma 2)*

*Proof of completeness.* Suppose there exists an augmenting path starting from root in G. By Lemma 2, the algorithm cannot return nil. Therefore, the algorithm will eventually return a path, which is an augmenting path by soundness.

*Q.E.D. (Completeness)*


### Improvement

In practice, we do not have to write `find_augmenting_path` in a recursive way.

### References
1. Wikipedia page of the Blossom algorithm: <https://en.wikipedia.org/wiki/Blossom_algorithm>
2. Tarjan, Robert, "Sketchy Notes on Edmonds' Incredible Shrinking Blossom Algorithm for General Matching", Course Notes, Department of Computer Science, Princeton University <https://stanford.edu/~rezab/classes/cme323/S16/projects_reports/shoemaker_vare.pdf>

```
r --- a === b
|          / \
x === y---c===d --- t

P:  r --- x === y --- c === d --- t
```

# Action generation

Action generation combines candidate moves into a turn that advances the
Present and leaves the moving player's royals safe. For piece destinations,
see [move-generation.md](move-generation.md).

## Files

- [`hypercuboid.h`](../../src/core/hypercuboid.h): `HC_info` and its public action-generation API.
- [`hypercuboid.cpp`](../../src/core/hypercuboid.cpp): candidate construction, consistency and safety checks, and conversion to move sequences.
- [`hypercuboid.inl`](../../src/core/hypercuboid.inl): search implementations and ordering support.
- [`graph.h`](../../src/misc/graph.h): graph and matching API.
- [`graph.cpp`](../../src/misc/graph.cpp): matching implementation for pairing departures and arrivals.
- [`geometry.h`](../../src/misc/geometry.h): `point`, `HC`, `slice`, and `search_space` APIs.
- [`geometry.cpp`](../../src/misc/geometry.cpp): search-space partitioning and removal of invalid regions.

## Public API

- `HC_info::build_HC(state)`: returns the search context and its matching search space.
- `info.search<Ordering, hc_policy::deferred>(space, order)`: lazily enumerate legal `moveseq` results. The ordering is deduced from the supplied object, or defaults to natural ordering; the policy defaults to Deferred. Other policies are `hc_policy::iterative`, `hc_policy::stable`, and `hc_policy::balanced`.
- `info.line_to_axis(line)`: returns the axis for an existing playable timeline or a prospective branch timeline, or `std::nullopt` if none exists.
- `info.get_semimove(axis, coordinate)` and `get_move_boards(axis, coordinate)`: inspect candidates and their result boards.
- `info.take_point(hc)`, `find_problem(point, hc)`, and `to_action(point)`: select a candidate, identify invalid regions, and convert a consistent candidate to a move sequence.
- `graph::add_edge(u, v)` and `find_matching(include)`: construct pairings and find a matching covering the required vertices.
- `HC::contains`, `intersects`, `split`, and `remove_slice`; `slice::fix_axis`; and `search_space::contains`, `concat`, and `prune_empty`: inspect and partition the candidate space.

Use a search space with the context returned by the same `build_HC` call.
Keep that context alive and unchanged while consuming its generators.

For algorithmic details, please refer to [hypercuboid.md](../hypercuboid.md)
and [graph-matching.md](../graph-matching.md).

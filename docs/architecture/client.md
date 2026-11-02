# Client layer

The client layer in [`src/client/game.h`](../../src/client/game.h) and [`game.cpp`](../../src/client/game.cpp) provides a `game` object for interactive use. It combines the [core game logic](core.md) with history navigation, individual move editing, comments, and PGN import/export. Python and WebAssembly bindings expose it to interfaces.

## Game history and variations

A `game` owns a tree of `gnode<comments_t>` nodes and keeps a pointer to the currently selected node. The node implementation lives in [`src/core/gametree.h`](../../src/core/gametree.h); each node records an action, an optional resulting state, child variations, and comments supplied by the client.

`visit_parent()` moves back through submitted actions. `get_child_actions()` lists
continuations. The child operations have separate responsibilities:

- `has_child(action)` checks membership without formatting or returning node pointers.
- `visit_child(action)` follows an existing continuation and returns false without
  mutation if it is missing. It never creates a child.
- `add_child(action)` validates and inserts a new continuation without navigating
  or clearing draft moves and redo history. It returns false for duplicate or
  invalid actions.
- `delete_child(action)` removes a recorded continuation and its descendants.

`get_historical_actions()` returns the actions on the path from the root to the
current node. Python and WebAssembly expose the same child-operation names.
Callers that previously relied on `visit_child` to create a branch must now add
it explicitly before visiting; `submit()` still inserts new submitted actions.

Each node keeps an owning vector in presentation/PGN order and a secondary
`std::set` of child pointers compared by action contents. Internal `find_child`
returns the child pointer or null through a transparent lookup by action.
`ext_move::operator<` orders moves by coordinates and promotion.
`action::operator<` uses that ordering to compare canonical move sequences;
for siblings, their shared parent state uniquely determines `branching_index`.
Lookup takes logarithmically many action comparisons. Listing and vector erasure
remain linear, and deletion also destroys the removed subtree.

Insertion updates both containers with rollback if vector allocation fails.
Cloning rebuilds the index against the cloned nodes. Actions stay immutable while
indexed. Each action has exactly one child in both containers.
`gnode::add_child` returns null on duplicate insertion and preserves the existing
child; `game::add_child` returns false for the same case.

PGN loading constructs history with last-occurrence replacement of duplicate
actions. See [Game history construction](parsing.md#game-history-construction)
for replacement order, subtree validation, and endpoint selection.

## Moves within a turn

The client maintains a separate cache of states and moves for the turn being edited. `get_unmoved_state()` returns its starting state, and `get_current_state()` returns the preview state when one exists, otherwise the state at the cache cursor.

`apply_move()` normalizes promotion and checks whether the move can be applied using the core logic. On success, it appends the resulting state and move, discarding cached entries after the cursor. `undo()` and `redo()` move through the cache. These operations are distinct from navigating submitted actions in the game tree.

`can_submit(allow_submit_with_checks = false)` tests turn completion and royal
safety for the real draft. Passing `true` tests only completion.
`submit(allow_submit_with_checks = false)` uses the same policy; rejected
submission preserves the recorded draft and history. `get_submit_action()` is a
completion-only extractor: it returns the canonical action through the current
cache cursor even if it exposes a royal, excluding redo moves and previews. `submit()` inserts that action when missing and visits its child node, preserving existing comments and descendants when found. It caches the validated submitted state for a new child. Moving to a history node refreshes the move cache for that node.

## Temporary move previews

`preview_move(move)` replaces any previous preview with the legal successor of
`now->first`, returning whether calculation succeeded. An invalid move clears the
previous preview. The preview lives outside the move cache and game tree;
`discard_preview()` releases it and silently does nothing when no preview exists.
Both methods are exposed by the WebAssembly binding.

Board-display queries read `get_current_state()`: boards, present, timeline status,
checks, phantom boards/checks, board size, and movable-piece highlights.
Playability and destination generation also read the preview state. Actual move
application and `can_submit()` use `now->first`; submission discards the preview
and submits only the recorded draft moves.
Cached moves, undo/redo availability, history, suggestions, and PGN remain based
on the real position. The worker supplies the temporary move's arrows separately.

`apply_move()`, `undo()`, `redo()`, `submit()`, `visit_parent()`, and continuation
insertion discard previews before proceeding, even when the operation fails.
`visit_child()` discards previews only on successful navigation; a missing child
leaves the preview, draft, and history unchanged.
There is no preview-commit method: applying the move normally recalculates it
from the real state. Previewing and discarding preserve existing redo history.

## Interface data and notation

The client exposes boards, playable pieces, move destinations, checks, phantom boards, match status, and promotion options. Interfaces use these queries to display positions and offer interactions without implementing the game rules themselves.

[Parsing and printing](parsing.md) describes `game::from_pgn()` and `show_pgn()`,
including loading policies, validation diagnostics, and export options. Comments
are attached to history nodes; game headers are stored in `metadata`.

## Illegal history

`game::from_pgn()` defaults to strict loading. The
[loading policies](parsing.md#loading-policies) describe how
`game::load_options::allow_illegal_history` permits importing exposed royals
without changing the policy for subsequent manual editing.

Each `gnode` stores `has_illegal_history()`. The root is classified from its
setup. A child is flagged when its parent is flagged or its submitted position
fails royal safety. Descendants inherit the flag even when subsequent actions resolve the
royal exposure. Flags are immutable after creation and preserved by
cloning; ancestors and siblings are unaffected. Low-level `gnode::create_root`
and `create_child` accept an optional flag, defaulting to false for trusted legal
callers. These factories do not repeat legality checks. The `game` insertion and
loading paths classify positions before passing that flag.

Manual `game::submit()` rejects unresolved royal exposure by default.
`submit(true)` permits it after requiring turn completion and records the
illegal-history flag. Existing illegal history does not automatically enable
this permission. Reusing a child retains its flag.
`game::add_child()` checks royal safety for unflagged parents; on a flagged branch
it checks move applicability and propagates the flag without another safety scan.
Neither path marks temporary drafts or previews as submitted illegal history.

`game::has_illegal_history()` describes the selected submitted node.
`get_child_actions()` returns `game::action_summary` records with `act`, `pgn`,
and `is_illegal`; the flag describes the destination node, including inherited
illegal history. Python exposes those same named fields; WebAssembly uses
`action` for the action field and preserves `pgn` and `is_illegal`.
`visit_child()` remains an existing-only navigation primitive without a UI
permission policy. Returning to an unflagged ancestor restores normal analysis.

For flagged nodes, `suggest_action()` returns `nullopt` immediately and
`get_match_status()` returns `match_status_t::ILLEGAL_HISTORY`, without invoking
Hypercuboid. PGN tree/path export and continuation labels suppress computed mate
symbols and outcomes for flagged nodes, and flagged children cannot serve as
legal-continuation witnesses for their parents. Mate classification of legal
ancestors remains available. Result tokens are omitted before continuations,
including illegal continuations after checkmate, to keep exported PGN reloadable.
Moves, comments, variations, and user-supplied metadata remain preserved.

### Binding and UI integration

Python exposes `game.from_pgn(pgn, allow_illegal_history=False)`, both history
queries, and `match_status_t.ILLEGAL_HISTORY`. WebAssembly supports
`from_pgn(pgn)` (strict) and `from_pgn(pgn, options)`, where the optional JavaScript
object accepts boolean `allowIllegalHistory` and `initialPositionOnly` properties.
Omitted or undefined properties retain the C++ load-option defaults. For example,
`from_pgn(pgn, { allowIllegalHistory: true })` enables permissive loading. The
initial-only option returns the setup without processing history; combining it
with `allowIllegalHistory: true` also bypasses setup legality checking for display
preparation. Invalid option types throw JavaScript `TypeError`s. PGN loading
retains the existing `{success, game, message}` result convention. It exposes both history
queries and formats the flagged match status as `White's Move (Illegal History)`
or `Black's Move (Illegal History)` from the selected node's mover.

The JavaScript UI must supply its checkbox value when loading. Before Next it
must inspect the destination flag and, when the checkbox is off, show a message
and leave the position unchanged. When viewing flagged history, the checkbox
must stay checked and disabled. Returning to legal history unlocks it without
changing its checked value. Only the user enables or disables the option; there
is no saved preference or automatic enabling. This permission policy belongs to
the UI and is not enforced by the C++ navigation methods. For manual submission,
the worker passes the checkbox value to both `can_submit()` and `submit()`.
Failed loading must preserve the old game.

## Deleting continuations

`gnode::delete_child(const action &)` erases one matching direct child and owns
recursive destruction of its descendants. It returns false when absent and
preserves the order of remaining children. `game::delete_child(const action &)`
exposes this operation at the current node. Successful deletion discards the
preview but preserves the current node, draft moves, undo/redo cache, and legal
move availability. Missing children return false without mutation.

The WebAssembly binding exposes singular `delete_child`, `visit_child`,
`has_child`, `add_child`, and `get_submit_action`. Pending deletion and restoration are entirely in the UI;
C++ stores no tombstones or restoration history.

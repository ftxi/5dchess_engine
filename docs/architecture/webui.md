# Web UI architecture

The static WebAssembly analyzer lives in [`ui/`](../../ui/). Its browser entry
point is `index.html`; the main thread manages DOM controls and canvas rendering,
while a module worker owns the compiled game object. The Python-hosted interface
in `extern/client` is a separate application. Build and local-server instructions
remain in [Building](../usage/building.md#webassembly).

## File structure

| File or directory under `ui/` | Responsibility |
| --- | --- |
| `index.html` | Panel/dialog markup, inline SVG icon definitions, piece asset mapping, theme names, import map, and the `main.js` entry point |
| `scripts/main.js` | Connects UI callbacks and board interactions to worker messages; applies responses to the display |
| `scripts/startup.js` | Starts the worker before piece loading and queues messages until UI setup finishes |
| `scripts/selection.js` | Local piece selection, temporary position requests, rollback, and deferred move application |
| `scripts/ui.js` | Desktop controls, dialogs, keyboard shortcuts, comment editing, settings, continuation selection, and export requests |
| `scripts/keyboard.js` | Shortcut routing, editing/dialog guards, repeat handling, and Space dispatch |
| `scripts/navigation.js` | Nearest-board and directional searches in displayed grid coordinates |
| `scripts/mobile.js` | Moves the same controls into the compact shell and manages its open sheet |
| `scripts/metadata.js` | Formats headers and controls metadata visibility and expansion |
| `scripts/worker.js` | Loads the Wasm module, owns the game and private next hint, handles commands, and assembles display data |
| `scripts/canvas.js` | Camera animation, viewport sizing, display density, mouse/touch gestures, and render scheduling |
| `scripts/draw.js` | Chess-specific board geometry, visible-board filtering, pieces, labels, highlights, arrows, and focus points |
| `scripts/level-of-detail.js` | Shares raster LOD generation and selection for pieces and board surfaces; manages surface caches and the composition buffer |
| `scripts/color.js` | Reads theme colors for canvas drawing and resolves CSS color values |
| `scripts/parse.js` | Parses board FEN into cells for rendering |
| `scripts/hash.js` | Hashes actions and history paths for remembering navigation choices |
| `scripts/filename.js` | Builds export filenames from game metadata |
| `styles/`, `assets/`, `fonts/` | Layout/style rules, piece SVGs, and bundled label fonts |
| `wasm/` | Generated `engine.js` loader and `engine.wasm`; also contains a binding smoke script |
| `test/` | Dependency-free Node regressions; see [test instructions](../../ui/test/README.md) |

The Emscripten CMake build copies `ui/` into its build directory and emits the
compiled module into that copy's `ui/wasm/`. The HTML import map resolves the
main-thread modules' short import names. The worker uses relative imports.

## User, main thread, and worker communication

`main.js` imports `startup.js` before the renderer. That module starts
`worker.js` with `new Worker(..., { type: 'module' })`, allowing Wasm initialization
to overlap the renderer's awaited piece loading. It queues worker messages until
`main.js` has initialized the compact layout, chess canvas, and UI callbacks,
then delivers them in order. The worker imports `wasm/engine.js` and creates the
compiled module. It reports `engine_version` and `ready`; after handling `ready`,
the main thread sends the saved settings and loads the standard turn-zero position.

The worker accesses C++ through [`emmodule.cpp`](../../emmodule.cpp), which
binds [`game`](../../src/client/game.h) and converts board coordinates, actions,
headers, and query results to JavaScript objects. These objects cross the
worker boundary through `postMessage`; the Wasm game itself stays in the worker.
The frontend consumes [Branched 5DPGN](../branched-5dpgn.md) strings for import,
export, and displayed notation rather than implementing chess legality.

| User interaction / main-thread command | Worker work and response |
| --- | --- |
| Select a piece: `gen_move_if_playable` | Queries legal destinations and returns `moves`; the main thread highlights them |
| Preview a touch destination: `preview_move` | Calculates a temporary successor and returns normal display data tagged with `previewId` |
| Cancel a touch destination: `discard_preview` | Drops the matching preview and returns the real position |
| Select a destination: `apply_move` | Applies the draft move, updates button availability, and returns rendered-position data |
| Submit, Undo, Redo: `submit`, `undo`, `redo` | Mutates the game's turn-editing state and returns updated buttons and boards |
| Previous/Next: `prev`, `next` | Navigates submitted history; Next includes the selected action |
| Hint: `hint` | Reveals the cached action, prepares another unseen action, and refreshes continuations and button state |
| Import: `load` | Parses PGN, replaces the game on success, and refreshes the position, headers, comments, status, and continuations |
| Comment update: `update_comment` | Updates the node's comments and returns the HUD state |
| Settings: `update_settings` | Merges worker settings; notation changes refresh continuation strings |
| Export: `export` | Formats PGN and returns `update_pgn` with the request identifier |
| Display refresh: `view` | Reassembles visible game data without changing the game |

The `data` response contains boards, present, focus, board size, highlights,
phantom boards, and phantom-check arrows. The worker parses each board's FEN;
`main.js` supplies the resulting data to `draw.js`. Separate responses update
controls: `update_buttons` carries availability, `update_select` carries action
objects and their formatted strings, and `update_hud_status` carries status,
headers, and comments. Errors are reported with `alert`.

Legal-move requests and their `moves` replies carry a selection `requestId`.
The main thread ignores replies for canceled or superseded selections and draws
selection feedback locally without requesting a full `view`. Worker `data`
responses mark position-changing commands with `positionChanged`; these clear
pending gestures and selection, while plain display refreshes preserve them.

Continuation text and actions remain paired: the DOM displays `pgn` strings,
while `main.js` keeps the corresponding action array for Next. The worker
remembers previously visited continuations using hashes of the history path.
A formatting-only refresh requests preservation of the current selection.

### Status and hidden hints

When entering a node, the worker calls `suggest_action()` and privately stores
its optional result in JavaScript. That call searches for a legal action absent
from the node's children but does not insert it. In C++, `game` stores
`std::optional<bool> nonterminal`: unknown until searched, true when a legal
witness exists, or false when none exists. Node navigation resets this flag;
partial move editing does not. `get_match_status()` searches only when the flag
is unknown, then distinguishes checkmate from stalemate through phantom checks
when it is false.

With a pending hint, the worker displays the node's move status without asking
for match status again. With no pending hint, it calls `get_match_status()`;
the flag avoids repeating the hypercuboid search. Having no unseen action can
still mean PLAYING when every legal action is already a child.

Hint calls `add_child()` with the stored action. This validates and adds
the continuation without navigating or clearing draft moves and redo history.
The worker then prepares the next suggestion and disables Hint when exhausted.
Adding a hint retains the selected action and keyboard focus, then scrolls the
continuation list (or compact sheet) to the bottom. The first hint in an empty
list is selected automatically. Explicit deselection after deleting the selected
branch remains intact. Other list refreshes do not trigger automatic scrolling.
Only revealed hints appear in the game tree and exports. The node's move title
is retained across hint insertion so draft moves do not change whose turn is
reported. Comment changes reuse the pending hint and status cache.

## Assets and CSS

### Pieces, labels, and canvas density

The canvas renders at the display's pixel density, including after browser zoom
or moving between displays. Piece detail is selected using physical pixel size;
camera movement and pointer coordinates remain in CSS pixels. At smaller zoom
levels, pieces use raster images cached once in separate canvases at six sizes
(32, 16, 8, 4, 2, and 1 pixels), retaining level-of-detail rendering without
converting the cache to `ImageBitmap` objects.
Timeline and turn labels use upright Playfair with lining numerals. The bundled
`PlayfairLining-Regular.woff2` is a static instance at weight 400, optical size 14,
and width 100. Its digit character mappings use the upstream `lnum` glyphs so
canvas text renders lining numerals without relying on CSS OpenType settings.
Timeline labels align the visible top of `L` exactly with the upper edge of the
multiverse grid square, using the font’s measured text bounds.

Board row and column labels use locally bundled Playfair Italic at regular weight
with a small-text optical size of 14. Rank digits use tabular old-style figures
(`onum` + `tnum`), baked into the digit mappings of
`PlayfairItalicTabularVF.woff2` for canvas rendering. Column labels
share a baseline with room for descenders such as `g` inside the squares.
Row and column labels align their visible left edges with a 0.05em inset inside
each square. Row labels retain a 0.1em top inset, accounting for the italic
numerals' different heights and overhangs.
Playfair is bundled in `ui/fonts/` with its SIL Open Font License, which permits
commercial use. The font and license come from the
[official Playfair repository](https://github.com/clauseggers/Playfair/tree/b869b3a82f7188b4e790f9ef1ebe2d11630bde8f).

Board surfaces share the piece LOD sizes and selection thresholds. Their images
contain the margin and playable squares, excluding pieces, highlights, and
file/rank labels. The renderer supplies the same drawing functions used for
direct drawing. All levels are prepared together for each surface appearance;
opacity is applied only when the completed surface is drawn. Highlighted boards
use direct drawing to retain highlights between margins and squares. Translucent
direct surfaces, including visible labels with their zoom fade, are composed in
a reusable display-resolution canvas before applying board opacity once. The
composition canvas is cropped to viewport bounds at high zoom.

Before full import, the worker calls
`from_pgn(pgn, { allowIllegalHistory: true, initialPositionOnly: true })` to obtain
the initial setup without parsing move history or checking setup legality. Structural
setup validation and existing lexer lookahead remain. It posts `initial_boards`
with dimensions, decoded boards, and the import request ID, disposes that temporary
game, then performs the full import with the actual legality setting. The main
thread prepares all surface levels while the worker processes history; early
boards never replace the displayed position. Cancelled or superseded preparation
is ignored. Named variants and custom setups are resolved in C++, and JavaScript
only decodes the returned board FENs. Endpoint data and appearance changes also
prepare any missing surface variants. Retained board raster pixel data is bounded
to approximately 8 MiB; missing entries fall back to direct drawing.

### Theme data

The color theme in Settings applies to both the boards and the interface on
phones and desktops. Each theme defines the main action color, muted text,
panel backgrounds and text, softer surfaces, and borders. Submit uses the same
main color in both layouts. Desktop panels retain their translucency. Vintage
uses an olive palette. Plain keeps a neutral paper-and-graphite palette with
light gray multiverse cells, darker gray board frames, and stronger square
contrast. Dark uses a midnight palette: ink-blue multiverse cells, ice-blue
frames for White's boards, violet frames for Black's boards, and cool slate
squares. Both board frames remain distinct against either multiverse cell.
L/T labels use the opposite multiverse cell color in every theme, retaining
their existing appearance where they meet the grid boundaries.
Plain's interface uses Material 3 Monochrome; Dark's uses Tonal Spot with seed
`#4f6b91`, both generated at standard contrast using version 0.3.0 of
[Material Color Utilities](https://github.com/material-foundation/material-color-utilities).
The generated values are stored as CSS variables, without a runtime library.
Plain marks the present in muted gold, with amber White moves and blue Black
moves. Dark marks it in indigo, with cyan White moves and rose Black moves.
Generated moves use green/teal, and check uses the Material error color.
The shared palette is defined in
`ui/styles/color-schemes.css`; hover and pressed colors follow it automatically.
Scrollable panels, dialog content, and form controls share thin, theme-colored
scrollbar thumbs with transparent tracks and corners, so no rail is visible.
WebKit and Chromium use `::-webkit-scrollbar` pseudo-elements with native
scrollbar properties reset to `auto`. Firefox uses the standard scrollbar color
and width properties even when it accepts WebKit selectors in feature queries.

### Stylesheet responsibilities

The HTML loads styles in this order:

| Stylesheet | Responsibility |
| --- | --- |
| `color-schemes.css` | Board and semantic interface color variables for each theme |
| `main.css` | Font declarations, document/canvas sizing, and shared page rules |
| `panels.css` | Desktop HUD, navigation, continuation list, metadata, toolbar, and debug presentation |
| `popup.css` | Import/export/settings/help dialogs and form controls |
| `mobile.css` | Compact shell, sheets, touch controls, and overrides for reparented controls/dialogs |

Canvas colors are resolved from CSS variables through `color.js`; changing a
theme refreshes the canvas palette as well as the DOM. SVG piece files are mapped
by piece code in `index.html`; interface icons use SVG symbols referenced
with `<use>`. The existing Remix Icon symbols live in the
[Remix sprite](../../ui/icons/remix.svg), including dynamically switched compact
comment and metadata icons. Desktop history controls and continuation Delete/Restore use
the bundled [Lucide sprite](../../ui/icons/lucide.svg). Its source revision is
recorded in the sprite, and the [Lucide/Feather license notices](../../ui/icons/LUCIDE-LICENSE.txt)
ship beside it. The compact navigation retains its existing icons.

## Behavior of UI components

### Board canvas

The generic camera and gesture layer lives in `canvas.js`; `draw.js` adds board
coordinates, flipping, focus, and chess-specific rendering. Draft moves,
historical moves, selectable destinations, ordinary checks, and phantom checks
are drawn as separate highlights and arrows supplied by the worker. Phantom
visibility is controlled locally and requests refreshed display data.

Arrows retain their normal opacity while either endpoint is inside the viewport.
When both endpoints leave it, opacity smoothly decreases with the distance of
the nearer endpoint from the viewport, reaching zero at 20% of the larger canvas
dimension. This distance scales with the canvas size and stays consistent across
zoom levels and display densities. The coarse arrow filter includes that fade
margin, and fully transparent arrows are skipped
before creating their paths or gradients. Panning or zooming back restores opacity
using the same distance rule.

Spatial opacity fades for arrows, L/T labels, and file/rank labels use a shared
`safeOpacity` helper as a workaround for intermittent Safari flashes near opacity
endpoints. It preserves alpha in the inclusive range `[0.002, 0.998]`, snaps lower
values to zero and higher values to one, and treats non-finite values as zero.
It is separate from the easing curve and applies to final alpha, including the
arrow's `0.8` opacity factor. Zero-opacity arrows and labels skip drawing entirely.

Flip reverses timeline order and both ranks and files inside each board, giving
Black's perspective. Under Settings → Interface, **Do not reverse files when
flipped** keeps files running left to right while retaining the flipped ranks
and timeline order. The checkbox defaults to off and is saved in browser storage.
Changing it redraws board contents immediately without moving boards or the
camera. Pieces, square colors, coordinate labels, highlights, arrows, and click
coordinates all follow the selected orientation; artwork and text stay upright.

### Canvas keyboard navigation

F retains the Focus button's cycle through game-provided focus points. Space
centers the nearest visible board at the normal Focus zoom. If the camera is
already at a normal Focus view, Space widens around that board to show at least
a 3 × 3 neighborhood, including gaps; its zoom is at most 40% of the Focus scale.
There is no timing window. During motion, the destination is checked instead,
so rapid presses can widen the same board before the first animation finishes.
Position matching allows 0.5 CSS pixels per axis and zoom matching allows a
0.1% relative difference. From the wide view, Space returns to normal Focus.
Shift+Space requests the wide view directly. Holding Space or F does not repeat.

W/A/S/D moves up/left/down/right while preserving zoom. Held keys combine into
a direction vector: W+A is up-left, opposite keys cancel per axis, and releasing
one key continues along the remaining direction. The initial step waits up to
40 ms to recognize a chord, or commits on key release for shorter taps. Chords
within that window select the same diagonal regardless of key order.

Navigation starts at the grid position nearest the camera target, uses displayed
coordinates after flipping, and skips gaps to the closest visible board on the
cardinal or diagonal ray. If there is none, it advances one grid step into empty
space. Visible phantom previews participate; hidden previews are excluded.

Holding starts automatic movement after 250 ms, accelerating over 1.5 seconds
from four to ten horizontal board spacings per second at normal Focus zoom.
The speed budget is measured in screen distance: smaller zoom means more grid
steps per second, and diagonal travel is normalized by its actual step length.
Fractional steps accumulate across frames; distant views batch multiple steps
into one destination, including sparse-board skips. Adding or releasing a key
preserves acceleration. Browser key repeat does not drive movement. Release of
all keys stops further destinations; focus changes, pointer/wheel activity,
window blur, and blocked input cancel held keys and the pending chord. All
commands use the camera `moveTo` path and do not select pieces or modify the game.

Camera position and logarithmic zoom use a corrected exponential curve with a
finite duration, ending exactly at the destination with zero velocity. Duration
is `ln(1 + G) / k` milliseconds, where
`G = max(abs(dx)/0.1, abs(dy)/0.1, abs(targetScale-currentScale)*ln(2)/ln(1.02))`.
Position differences use world coordinates directly, and the gauges reproduce
the original stopping tolerances: 0.1 world units and approximately 2% zoom.
Scale is logarithmic base two, so multiplying its difference by `ln(2)` gives
`ln(newZoom / oldZoom)`. At the default decay rate, one horizontal board
(100 world units) takes about 747 ms, and a zoom-only doubling or halving takes
about 388 ms. Duration is independent of starting zoom for equal world movement.
The decay constant matches the original smoothing at 60 FPS. Motion is the
analytic solution of `dc/dt = lambda(T-t) * (target-c)`, where
`lambda(tau) = k * expm1(k*tau) / (expm1(k*tau) - k*tau)`.
The remaining error is proportional to
`(expm1(k*(T-t)) - k*(T-t)) / (expm1(k*T) - k*T)`. Position and
logarithmic zoom share this response on every frame, including after a target
change. Retargeting starts from the current camera and recomputes duration;
there are no independent velocity-preserving curves per component. All axes
start and decelerate together, and reach zero remaining error and velocity at
the endpoint. Small durations use a series expansion to avoid numerical
cancellation. Frame subdivision does not change the trajectory. The same
duration rule applies to held navigation.

Mouse and touch drags take over from the rendered camera, use the starting drag
zoom for pointer displacement, cancel pending zoom, and restart the shared
response as pointer targets change. Rendering continues until the exact endpoint.

Shortcuts are available outside text entry without requiring canvas focus.
Editable descendants, form fields, IME composition, browser modifier commands,
open dialogs, and the compact tools menu take priority. Native Space/Enter
activation on buttons is retained. Pointer activation of game-navigation
buttons returns focus to the canvas; keyboard activation retains button focus.
Clicking the canvas explicitly focuses it without scrolling the page. The
editor's caret and an opaque, uniform theme tint across its status/comment panel
show only while the editor has focus. In compact layout, the comment sheet receives
the tint. The editing surface mixes 10% primary color into the panel color to
separate text entry from the busy canvas. Focusing other panel controls does not activate it. Leaving the editor
preserves the draft and remembers the text selection for keyboard return. The
canvas and editor have no large focus outlines; the canvas remains in the normal
Tab order, and buttons retain their keyboard focus indicators.

### Desktop panels and metadata

Game information, status, and navigation share a top-aligned grid with 24-pixel
outer margins and gaps. Metadata stays at the top left and the 200-pixel-wide
navigation panel stays at the top right. Status and the bottom toolbar share
the full viewport's horizontal center; the toolbar sits 24 pixels above the
bottom edge. The canvas remains full width beneath these floating panels.
The status title is centered independently of the light, which sits 16 pixels
from the right edge. Symmetric header padding reserves space for the light; long
status titles truncate rather than overlap it.

The HUD is at most 440 pixels wide, narrowing to 244 pixels at a viewport width
of 1100 pixels to leave a 24-pixel gap beside expanded metadata. Its width and
center stay unchanged when metadata is hidden, shown, or expanded. Metadata
expands from its fixed top-left anchor to 380 pixels wide; oversized details
scroll. The expanded navigation panel is 500 pixels tall and fits within the
minimum 600-pixel viewport height; its continuation list scrolls internally.

The metadata summary displays White, Black, and recorded Result. Expansion
reveals Event, Site, Date, and Round. Full values wrap; summaries truncate long
names. Metadata titles show the full value only when visible text is ellipsized.
Resize observations and font-load events refresh titles after size and layout
changes; wrapped values, placeholders, and visible results have no titles. Missing player names become “Unknown Player”, missing details become a
dash, and missing or unfinished results use a muted slash in the expanded view.
The recorded Result is independent of the current node's match status.

Metadata is hidden when Event, Round, White, Black, Site, Date, and Result are
all missing or unknown; an unfinished `*` Result does not count. The saved
**Show metadata panel** setting also controls visibility and hides the phone's
game-details button when disabled. Expansion persists until folded, and loading
a game replaces the displayed headers.

Expand/fold controls have fading hover/focus highlights: a corner highlight for
metadata and centered shallow highlights for HUD and navigation (480 × 50 and
320 × 48 pixels respectively). Hint has its own neutral surface below the
continuations and highlights independently of the selected option.

The HUD and navigation expand/fold controls are native buttons with accessible
names, controlled-region references, and synchronized expanded states. Tab
focuses them and Enter or Space activates them without triggering game shortcuts.
Their normal and hover appearance is unchanged; keyboard focus adds an inset
outline. Automatic comment expansion also updates the HUD button state. Hidden comment content,
inactive comment actions, and collapsed continuations are excluded from keyboard
focus; focusing invisible content must not scroll a collapsed panel.

### Continuations and move notation

The worker formats child actions through `get_child_actions(flags)`. The saved
Game setting **Use shortened notation for moves** defaults to enabled and adds
`SHOW_SHORT` to the capture/promotion flags. Toggling it refreshes the summary
and list while preserving the selected action and position. Export's **Short
Notation** preference is independent.

The compact summary is a single line constrained by the viewport, with CSS
ellipsis for overflow. The expanded list retains full text and wraps even
unbroken move strings. Selecting an option changes the selected continuation;
Next submits its action to the worker for navigation.

### Comments

The HUD's editable comment area tracks the current node's comment, draft edits,
and Update/Discard actions. Worker updates refresh the HUD text; saving sends
`update_comment`. The shared comment reader preserves editable block boundaries,
blank lines, and Shift+Enter breaks while ignoring trailing browser caret
placeholders. Saving, dirty-state checks, validation, and draft reads use the
same text representation. Automatic comment expansion is suppressed in compact mode.
The compact panel heading and matching accessibility labels use “Comment”.
Its header icon reflects open/closed state and whether text exists, including
an unfinished edit. Changing the active compact sheet does not discard edits.

### Compact shell and dialogs

The static analyzer uses floating desktop panels only when the browser viewport
is at least 1100 CSS pixels wide, at least 600 CSS pixels high, and has a
width/height ratio of at least 4/5. All other viewports use the compact shell.
For example, 1280 × 1440 uses desktop panels, while 1400 × 1800 uses compact mode.
The shared media query controls panel placement and compact interaction behavior;
resizing across the boundary reuses the same controls and preserves game state.

On phones, the move/status display stays at the top and Previous, Undo, Submit,
Redo, and Next stay at the bottom with icons above their labels. **Choose** in
the continuation summary opens the continuations panel, where you can choose a
recorded action or request a Hint before using Next. The footer summary stays
on one line within the screen width and truncates long continuations with an
ellipsis; the expanded continuations panel shows the full text. The comment button in the
header, immediately left of the game-information button, opens the comment panel.
Its outline/fill indicates whether the panel is closed/open, and dots indicate
whether the current comment contains text, including an unfinished edit.
The three-dot icon at the right of the header opens the action menu without a
visible heading. It always uses filled dots, with a background highlight while
the menu is open. There
is no separate bottom menu row, giving the canvas more room. Only one panel opens
at a time. All four panels share a circular floating cross button with a theme-colored
soft background, a defined border, a raised shadow, and a 44-pixel touch
target. It sits above the panel in portrait and beside it in landscape, so the
action menu starts directly with its buttons. The other panels keep their useful
headings. Tapping the highlighted header icon or **Choose** again also closes its
panel. The tag button in the header opens the loaded game's metadata; its outlined icon becomes
filled while Game details is open. Comments keep the existing
Update/Discard behavior; switching panels does not discard an unfinished edit.
Automatic comment expansion is suppressed in the compact layout.

Panels reserve canvas space below the boards in portrait and beside them on short
landscape screens. The canvas preserves its center and relative zoom when the
available space changes. **Focus** stays in the board area's lower-right corner,
moving above the cross button when a portrait panel is open, and fits the focused board within that area; repeated presses cycle the focus
points. A newly loaded game is focused automatically on phones. Drag to pan,
pinch to zoom, and tap to select pieces and destinations. Releasing a drag or
pinch does not select a piece.

Double tap a visible board to center it at the normal Focus zoom, which fits the
board to the compact viewport. When that board is already at normal Focus zoom,
double tapping it shows the same wide neighborhood as Space; double tapping
again returns to normal Focus. Tapping another board focuses that board. The
camera destination is checked during animation, using the same position and zoom
tolerances as Space. Empty cells and hidden previews are not focus targets.

Double taps require two taps within 300 ms, within 24 CSS pixels, and on the same
visible board. Piece selection and legal destinations appear without waiting for
the double-tap window. A destination tap requests `preview_move` as soon as its
legality is known. The worker uses the normal board-display getters to render the
temporary position, including new boards, pieces, move arrows, and checks, and
tags the response with `previewId`. There is no temporary destination-square
highlight or board-appearance animation.

The frontend saves the real position. A recognized double tap restores it and
the previous selection immediately, asks the worker to discard the preview, and
focuses the originally tapped board. Gesture hit-testing uses the original boards
while a preview is visible. Canceled or superseded preview responses are ignored,
including later display refreshes of that preview. The camera and game controls
remain steady during previews.

At 300 ms the frontend applies the move normally, keeping the temporary position
visible until the real result arrives. There is no native commit API. If legal
destinations arrive after the window closes, the move is applied when available.
Taps on different boards or farther apart commit the earlier tap immediately and
preview the later one. Dragging, pinching, touch cancellation, window blur, and
mouse interaction discard pending previews and restore the previous selection.
Import, history navigation, and position-changing worker responses clear pending
selection and moves. Undo/redo history changes only when the move is applied.

Import, export, settings, and help dialogs fill the phone viewport with the same
warm background as the navigation bars. A compact header places the title beside
a flat green **Close** text button with a 44-pixel touch target, subtle pressed
feedback, and a keyboard focus outline; content scrolls below it. Export has a single
**Options / Copy / Download** footer. Options opens a full-screen view on phones
and contains all export settings. Toggling settings does not calculate PGN.
Closing Options saves changed selections and requests one preview update;
closing without changes skips recalculation. Export returns immediately, showing
“Updating preview…” while keeping the previous preview visible. Copy and Download
are unavailable until the latest preview is ready. Rotating or resizing back to a
desktop-sized window restores the original controls without resetting the game.

### Import, export, and settings

Import reads pasted, selected, or dropped text and sends a `load` command.
Parsing and game creation occur in the worker. The main thread handles file
selection and display errors. Duplicate child actions in imported PGN use the
last occurrence, replacing the earlier branch's comments and descendants rather
than merging them. The surviving branch occupies its later position in the
continuation list.

`ui.js` owns dialog state and saved preferences. Settings callbacks forward
changes to the worker or apply them locally, such as theme, metadata visibility,
and automatic comment expansion. The continuation-notation preference is sent
with startup settings before loading the initial game.

Export requests carry identifiers so the UI can accept the matching PGN preview.
The worker builds formatting flags and calls `show_pgn()` with the selected
scope and metadata options. Mobile Options changes are applied together when
closed; unchanged options skip recalculation. Copy and Download remain disabled
until the latest requested preview is ready. Desktop keeps an explicit Refresh
workflow and editable filename/extension controls.

#### Export filenames

Mobile Download and the desktop default filename use the loaded game's headers:
`{event}_round_{round}_{white}_vs_{black}_{yyyymmdd}`. Missing Round omits
`_round_{round}`; missing Event omits both Event and Round. One missing player
becomes `anonymous`, while two missing players omit the match portion. Without
Event or players, the prefix is Site, or `game` when Site is missing too. Empty
values and `?` placeholders count as missing.

A complete, valid Date header is used; otherwise the browser's current UTC date
is used. Spaces, control characters, and characters unsafe in Windows filenames
become underscores; Unicode is preserved and normalized. Values that sanitize
to nothing count as missing. Reserved device names are prefixed with `game_`.
Long metadata is shortened to keep the entire filename within 180 UTF-8 bytes,
preserving the date and extension. Export options do not affect the default
filename. Desktop retains its editable filename and extension fields, Mate and
Short Notation controls, More Options panel, Save as File button, and explicit
Refresh workflow; closing desktop More Options does not regenerate the preview.

## Pending continuation deletion

The desktop navigation retains the cross arrangement inside a thin rounded
outline. Continuation text uses the full row width. Delete and Restore overlay
the trailing text on hover or keyboard focus; selection alone does not reveal
them. Pending rows use muted, struck-through text without changing their height.
Selecting a pending row is disabled. Marking the selected row clears selection
and disables Next until another active continuation is selected.

`branch-deletion.js` tracks exact action identities, including promotions, in a
set owned by the worker. Collapsing the panel leaves this set intact. Floating buttons below navigation offer Restore and Delete while the set is
nonempty; in compact mode these actions move into the fixed continuation header. Delete
commits directly without another dialog.

Previous, Next, Submit, and opening Export request confirmation when deletion
is pending. Cancel changes neither the tree nor pending edits. Confirmation
validates every target against current children before calling singular C++
`delete_child` operations, then performs the requested action. Position IDs and
confirmation tokens reject stale requests. Next uses `visit_child`
and cannot recreate missing branches. Submit validates its canonical action
first and excludes any matching pending branch from deletion, preserving that
branch's comments and descendants. Hint availability refreshes after deletion.

Export opening is guarded before displaying its window or calculating PGN.
Successful import replaces the game and clears pending edits without a deletion
prompt; failed import leaves the tree and pending edits intact.

The desktop navigation panel is 220px wide. Its cross has 4px gaps and 8px
inner padding, a 50% opaque pill surface over the 88% opaque panel (94%
combined opacity), and 24px icons. Submit uses the original
14px text and 72 × 48px button size. Pending-branch actions float below the
panel without a count or enclosing surface. They reuse the comment controls
at the same size and fade in/out over 250ms. Row action buttons have transparent
backgrounds and borders. A localized radial mask fades only the text beneath
the 40px action area, retaining 34% text opacity at the centre and smoothly
restoring full opacity at its edge. The mask follows hover/keyboard focus
visibility and is always applied when actions are visible in compact mode.
There is no painted patch where the area contains no text.


The compact continuation sheet is titled **Select Next Action**. Normal selection
hides row actions and their text masks. **Edit**, styled like the compact dialog
Exit control, reveals row actions and changes to **Done**. Done exits editing
when no deletions are pending. Once a deletion is staged, Restore/Delete replace
Done in the same 48px header. Delete is on the left and Restore on the far
right, matching the safe Edit/Done exit location. Both use the Edit text-control
style, with Delete in the theme error color. Restoring all branches, restoring the last pending
branch individually, or committing deletions returns to normal selection. Closing
or changing sheets exits ordinary editing but preserves pending deletions; reopening
shows their controls and row icons. Desktop controls retain their existing labels
and hover/focus behavior after switching between layouts.

Navigation controls expose their existing keyboard shortcuts through titles:
Submit [↵], Previous Action [↑], Next Action [↓], Undo [←], and Redo [→].
Accessible labels remain separate. Labeled Restore/Delete controls omit
redundant titles.

Desktop navigation availability uses tonal icons and four tapered crescents
on the enclosing pill. Each crescent follows its corresponding enabled
Previous/Next/Undo/Redo control. Filled SVG geometry narrows from a 2px centre
to pointed tips. Each extends 5 degrees past its diagonal junction, so adjacent
enabled crescents overlap and form a continuous accent. Four supplementary
2px quarter arcs fill the junctions at full width, each visible only when both
adjacent controls are enabled. Isolated crescents keep their pointed tips.
Navigation icons have no background, including on hover. Disabled icons and
Submit use lighter neutral gray, while enabled Submit retains its primary fill.
The arcs are decorative, ignore pointer events, and are hidden in compact mode.

### Inspecting illegal history in the analyzer

By default, importing PGN rejects setups or actions that leave the mover's royal
pieces exposed. Enable **Allow submit with checks** in Settings to load these
games or submit such an action manually. Other move and turn-completion rules
still apply.

Continuations with illegal history are highlighted in muted red. With the option
off, pressing Next on one shows a message and leaves the current position intact.
Enable the option manually to enter it. The status then includes **(Illegal
History)** and Hint is disabled, including for descendants whose own moves are
safe. The checkbox stays checked and locked while viewing illegal history.
Return to a legal ancestor, or load a game with legal history, to unlock it;
turn it off manually when finished. This permission is not saved across reloads.

### Import diagnostics

`import-diagnostics.js` owns import request IDs, retained diagnostics and temporary
or pinned highlights. The worker returns `load_result` without replacing the
open game. The main thread accepts results only for the active input snapshot,
then sends `commit_load`; `load_committed` clears diagnostics and closes the
import dialog. Editing or closing cancels prepared candidates through
`cancel_load`, and a newer request disposes of any previous candidate. Startup
loads omit request IDs and commit directly.

The decoration layer is behind the native textarea and ignores pointer events.
It shares typography and wrapping with the textarea; its viewport uses the
textarea's client dimensions to exclude native scrollbars, and its content
translation follows native scrolling. `ResizeObserver` synchronizes dimensions.
The import dialog reserves three lines for diagnostics, capped at 40% of its content
height on short viewports. Long messages scroll within that region, so error
length and validation state do not resize the editor or move the Load buttons.
The region stays reserved before the first result and after diagnostics clear.

A failed load shows an error icon followed by **Error at line …, … [Click to
Locate]:**, with inclusive column coordinates. The explanation appears below
this state line, and the editor receives an error-colored outline. Errors without
a source range show **Error:** without a highlight control. Hover or keyboard
focus anywhere on the full state line previews the range, and clicking anywhere
on it toggles the pinned state. Pinning changes the background of **[Click to
Locate]** without changing font weight or text width.
Missing-input diagnostics preview a caret instead.

Editing keeps the state line and explanation wording unchanged. It turns the
state line gray, clears the editor's error state, and disables the old location
and highlight until another result supplies a current location. During a retry,
the retained error has the same inactive presentation. Before any error exists,
validation shows **Checking import…**. The heading uses a live region.
Successful commit clears the diagnostic before closing the dialog.

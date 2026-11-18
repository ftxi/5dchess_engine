import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const source = await readFile(new URL('../scripts/selection.js', import.meta.url), 'utf8');
const { createSelection } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
const { parse_FEN } = await import('data:text/javascript,' + encodeURIComponent(
    await readFile(new URL('../scripts/parse.js', import.meta.url), 'utf8')));
const from = { l: 0, t: 0, c: false, x: 0, y: 1 };
const to = { ...from, y: 3 };

function fixture() {
    const messages = [], changes = [];
    const selection = createSelection({
        send: message => messages.push(message),
        onChange: state => changes.push(state),
        presentColor: () => false
    });
    const reply = (request, moves = [to]) => selection.receiveMoves({ requestId: request.requestId, moves });
    return { selection, messages, changes, reply, state: () => changes.at(-1) };
}

const { PendingBranches, actionKey } = await import('data:text/javascript,' + encodeURIComponent(await readFile(new URL('../scripts/branch-deletion.js', import.meta.url), 'utf8')));

test('selection feedback is immediate and legal destinations arrive before commitment', () => {
    const { selection, messages, reply, state } = fixture();
    const token = selection.preview(from);
    assert.deepEqual(state(), { selected: from, moves: [] });
    assert.equal(messages[0].type, 'gen_move_if_playable');
    reply(messages[0]);
    assert.deepEqual(state().moves, [to]);
    selection.commit(token);
    assert.equal(state().tap, undefined);
    assert.equal(messages.length, 1);
});

test('canceling a preview clears feedback and ignores late worker replies', () => {
    const { selection, messages, reply, state } = fixture();
    const token = selection.preview(from);
    selection.cancel(token);
    reply(messages[0]);
    assert.deepEqual(state(), { selected: undefined, moves: [] });
    selection.commit(token);
    assert.equal(messages.length, 1);
});

test('destination previews calculate immediately and cancellation restores selection without applying', () => {
    const { selection, messages, reply, state } = fixture();
    selection.click(from);
    reply(messages[0]);
    const token = selection.preview(to);
    assert.deepEqual(state(), { selected: undefined, moves: [] });
    assert.equal(messages[1].type, 'preview_move');
    assert(selection.acceptsPreview(messages[1].requestId));
    selection.cancel(token);
    assert.deepEqual(state(), { selected: from, moves: [to] });
    assert.equal(messages[2].type, 'discard_preview');
    assert(!selection.acceptsPreview(messages[1].requestId));
    selection.commit(token);
    assert(!messages.some(message => message.type === 'apply_move'));
});

test('a destination preview commits exactly one ordinary move and accepts its late preview until position changes', () => {
    const { selection, messages, reply } = fixture();
    selection.click(from);
    reply(messages[0]);
    const token = selection.preview(to);
    assert.equal(messages[1].type, 'preview_move');
    selection.commit(token);
    selection.commit(token);
    selection.cancel(token);
    assert.deepEqual(messages[2], { type: 'apply_move', from, to });
    assert.equal(messages.length, 3);
    assert(selection.acceptsPreview(messages[1].requestId));
    assert.equal(selection.preview(from), null);
    selection.reset();
    assert(!selection.acceptsPreview(messages[1].requestId));
    assert(selection.preview(from));
});

test('rapid destination taps calculate as soon as legal destinations arrive, without applying early', () => {
    const { selection, messages, reply } = fixture();
    selection.click(from);
    const token = selection.preview(to);
    assert.equal(messages.length, 1);
    reply(messages[0]);
    assert.equal(messages[1].type, 'preview_move');
    assert.equal(messages.length, 2);
    selection.commit(token);
    assert.deepEqual(messages[2], { type: 'apply_move', from, to });
});

test('legal replies arriving after commitment apply once, and reset cancels a waiting move', () => {
    const { selection, messages, reply } = fixture();
    selection.click(from);
    selection.click(to);
    assert.equal(messages.length, 1);
    reply(messages[0]);
    reply(messages[0]);
    assert.equal(messages.length, 2);
    selection.reset();
    selection.click(from);
    selection.click(to);
    const request = messages.at(-1);
    selection.reset();
    reply(request);
    assert.equal(messages.length, 3);
});

test('canceling restores an unresolved selection and accepts its eventual legal response', () => {
    const { selection, messages, reply, state } = fixture();
    selection.click(from);
    const token = selection.preview(to);
    selection.cancel(token);
    reply(messages[0]);
    assert.deepEqual(state(), { selected: from, moves: [to] });
    assert.equal(messages.length, 1);
});

test('empty legal responses, wrong-color taps and invalid destinations never apply moves', () => {
    const { selection, messages, reply, state } = fixture();
    selection.click(from);
    const invalid = selection.preview({ ...to, c: true });
    selection.cancel(invalid);
    reply(messages[0], []);
    assert.equal(state().selected, undefined);
    selection.click(from);
    reply(messages.at(-1));
    selection.click({ ...to, x: 7 });
    assert.equal(state().selected, undefined);
    assert(messages.every(message => message.type === 'gen_move_if_playable'));
});

test('stale selection responses cannot overwrite a newer selection', () => {
    const { selection, messages, reply, state } = fixture();
    const canceled = selection.preview(from);
    selection.cancel(canceled);
    selection.preview(to);
    reply(messages[0]);
    assert.deepEqual(state().moves, []);
    assert.deepEqual(state().selected, to);
    reply(messages[1], [from]);
    assert.deepEqual(state().moves, [from]);
});

test('main-thread wiring renders local feedback, preserves it on refresh and clears it on position changes', async () => {
    const messages = [], callbacks = {};
    let worker, highlights, displayed;
    let selectedIndex = -1;
    let selectConfig;
    const context = {
        structuredClone, actionKey, createSelection, initMobileLayout() {}, compactViewport: { addEventListener() {}, matches: false },
        window: {},
        Worker: class {
            constructor() { worker = this; }
            postMessage(message) { messages.push(message); }
        },
        ChessBoardCanvasBase: class {
            constructor() { this.infCanvas = { _cancelPendingTap() {}, startAnimation() {} }; }
            setSelectionHighlights(value) { highlights = structuredClone(value); }
            setData(value) { displayed = structuredClone(value); }
        },
        UI: new Proxy({ select: {
            setChangeCallback() {},
            getSelectedIndex: () => selectedIndex,
            setOptions(options, config) {
                selectConfig = config;
                selectedIndex = config.selection === undefined ? options.findIndex(option => !option.deleted)
                    : options[config.selection]?.deleted ? -1 : config.selection;
            }
        }, buttons: { setCallbacks(value) { callbacks.buttons = value; } } }, {
            get(target, key) { return target[key] || (value => { callbacks[key] = value; }); }
        })
    };
    const main = (await readFile(new URL('../scripts/main.js', import.meta.url), 'utf8')).replace(/^import .*;\n/gm, '');
    const startup = (await readFile(new URL('../scripts/startup.js', import.meta.url), 'utf8'))
        .replace(/^import .*;\n/gm, '').replace(/^export /gm, '');
    Object.assign(context, {
        document: { getElementById() { return null; } },
        reportLoaded() {}
    });
    vm.runInNewContext(startup, context);
    vm.runInNewContext('startWorker()', context);
    vm.runInNewContext(main, context);
    const canvas = context.window.chessBoardCanvas;
    const firstAction = [{ from, to, promote: 0 }];
    const secondAction = [{ from: to, to: from, promote: 0 }];
    const refresh = (options, extra = {}) => worker.onmessage({ data: {
        type: 'update_select', positionId: 1, options, preserveSelection: true, hintAdded: true, ...extra
    } });
    refresh([{ action: firstAction, pgn: 'first' }]);
    assert.equal(selectedIndex, 0); // The first Hint gets an immediately usable selection.
    refresh([{ action: firstAction, pgn: 'first' }, { action: secondAction, pgn: 'second' }]);
    assert.equal(selectedIndex, 0);
    selectedIndex = 1;
    refresh([{ action: secondAction, pgn: 'second' }, { action: firstAction, pgn: 'first' }]);
    assert.equal(selectedIndex, 0); // Identity survives reordering, not just its index.
    assert.equal(selectConfig.preserveFocus, true);
    assert.equal(selectConfig.scrollToBottom, true);
    selectedIndex = -1; // Explicit deselection after staging deletion must survive Hint.
    refresh([{ action: firstAction, pgn: 'first', deleted: true }, { action: secondAction, pgn: 'second' }]);
    assert.equal(selectedIndex, -1);
    refresh([{ action: secondAction, pgn: 'second' }], { hintAdded: false });
    assert.equal(selectConfig.scrollToBottom, false);

    const data = changed => worker.onmessage({ data: { type: 'data', data: { present: { c: false } }, positionChanged: changed } });
    data(true);
    const token = canvas.onPreviewSquare(from);
    assert.deepEqual(highlights[0].coordinates, [from]);
    worker.onmessage({ data: { type: 'moves', moves: [to], requestId: messages[0].requestId } });
    assert.deepEqual(highlights[1].coordinates, [to]);
    data(false);
    assert.deepEqual(highlights[1].coordinates, [to]);
    canvas.onCancelSquare(token);
    assert.deepEqual(highlights[0].coordinates, []);
    canvas.onClickSquare(from.l, from.t, from.c, from.x, from.y);
    callbacks.setImportCallback('new game');
    worker.onmessage({ data: { type: 'moves', moves: [to], requestId: messages[1].requestId } });
    assert.deepEqual(highlights[1].coordinates, []);
    data(true);
    assert.deepEqual(highlights[0].coordinates, []);
    canvas.onClickSquare(from.l, from.t, from.c, from.x, from.y);
    const request = messages.at(-1);
    worker.onmessage({ data: { type: 'moves', moves: [to], requestId: request.requestId } });
    const destination = canvas.onPreviewSquare(to);
    const preview = messages.at(-1);
    const resulting = { present: { c: false }, boards: [{ l: 0, t: 1, c: true, fen: 'new board' }] };
    worker.onmessage({ data: { type: 'data', previewId: preview.requestId, data: resulting } });
    assert.deepEqual(displayed.boards, resulting.boards); // Display before the timer commits.
    canvas.onCancelSquare(destination);
    assert.equal(displayed.boards, undefined); // Synchronous restoration of the saved position.
    worker.onmessage({ data: { type: 'data', previewId: preview.requestId, data: resulting } });
    assert.equal(displayed.boards, undefined); // A late result cannot flash a canceled board.
});

test('worker echoes selection identifiers and distinguishes refreshes from changed positions', async () => {
    const messages = [];
    const self = { postMessage: message => messages.push(structuredClone(message)) };
    const engine = { get_version: () => 'test' };
    const workerSource = (await readFile(new URL('../scripts/worker.js', import.meta.url), 'utf8'))
        .replace(/^import .*;\n/gm, '');
    vm.runInNewContext(workerSource, { self, PendingBranches, actionKey, parse_FEN, createModule: () => Promise.resolve(engine) });
    await Promise.resolve();
    let previewActive = false;
    self.game = {
        has_illegal_history: () => false,
        gen_move_if_playable: () => [to], apply_move() {},
        discard_preview() { previewActive = false; },
        preview_move() { previewActive = true; return true; },
        get_board_size: () => ({ x: 8, y: 8 }),
        get_current_boards: () => previewActive ? [{ l: 0, t: 1, c: true }] : [],
        get_current_present: () => ({ c: false, t: 0 }),
        get_current_timeline_status: () => ({ mandatory_timelines: [] }),
        currently_check: () => false, get_historical_actions: () => [], get_cached_moves: () => [],
        get_phantom_boards_and_checks: () => ({ boards: [], checks: [] }),
        can_undo: () => true, can_redo: () => false, has_parent: () => false, can_submit: () => false
    };
    self.onmessage({ data: { type: 'gen_move_if_playable', pos: from, requestId: 42 } });
    assert.deepEqual(messages.at(-1), { type: 'moves', moves: [to], requestId: 42 });
    self.onmessage({ data: { type: 'view' } });
    assert.equal(messages.at(-1).positionChanged, false);
    const before = messages.length;
    self.onmessage({ data: { type: 'preview_move', from, to, requestId: 43 } });
    assert.equal(messages.length, before + 1); // No button or history changes.
    assert.equal(messages.at(-1).previewId, 43);
    assert.equal(messages.at(-1).data.boards.length, 1);
    self.onmessage({ data: { type: 'view' } });
    assert.equal(messages.at(-1).previewId, 43);
    self.onmessage({ data: { type: 'discard_preview', requestId: 42 } });
    assert(previewActive); // An old cancellation cannot remove a newer preview.
    self.onmessage({ data: { type: 'discard_preview', requestId: 43 } });
    assert.equal(messages.at(-1).previewId, null);
    assert.deepEqual(messages.at(-1).data.boards, []);
    self.onmessage({ data: { type: 'apply_move', from, to } });
    assert.equal(messages.at(-1).positionChanged, true);

    // The worker reconstructs walls, including an entirely walled rank, from FEN.
    const parsed = Array.from({ length: 8 }, () => Array(8).fill('1'));
    parsed[2].fill('#');
    parsed[4][3] = '#';
    const wallBoard = { l: 0, t: 1, c: false, fen: '8/8/########/8/3#4/8/8/8' };
    self.game.get_current_boards = () => [wallBoard];
    self.game.get_phantom_boards_and_checks = () => ({ boards: [{ ...wallBoard, c: true }], checks: [] });
    self.onmessage({ data: { type: 'view' } });
    assert.deepEqual(messages.at(-1).data.boards[0].parsed, parsed);
    assert.deepEqual(messages.at(-1).data.phantom[0].parsed, parsed);
});

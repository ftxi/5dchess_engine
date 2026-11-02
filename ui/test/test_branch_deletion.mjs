import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';
const { PendingBranches, actionKey } = await import('data:text/javascript,' + encodeURIComponent(
    await readFile(new URL('../scripts/branch-deletion.js', import.meta.url), 'utf8')));
const move = x => [{ from: { l: 0, t: 1, x, y: 1 }, to: { l: 0, t: 1, x, y: 3 }, promote: 0 }];
const a = move(4), b = move(3);

test('pending branches distinguish promotions, deduplicate and preflight all targets', () => {
    const pending = new PendingBranches();
    const options = [{ action: a, pgn: 'e4' }, { action: b, pgn: 'd4' }];
    assert.notEqual(actionKey(a), actionKey([{ ...a[0], promote: 5 }]));
    assert(pending.toggle(a, options));
    assert(pending.has(structuredClone(a)));
    assert.equal(pending.plan(options).length, 1);
    assert.equal(pending.plan(options, a).length, 0);
    assert.throws(() => pending.plan(options.slice(1)));
    assert(pending.toggle(a, options));
    assert.equal(pending.plan(options).length, 0);
    assert(!pending.toggle(move(1), options));
});

async function workerFixture() {
    const messages = [], removed = [], navigated = [], loads = [];
    let options = [{ action: a, pgn: 'e4' }, { action: b, pgn: 'd4' }];
    const game = {
        get_child_actions: () => options,
        has_illegal_history: () => false,
        has_child: action => options.some(child => actionKey(child.action) === actionKey(action)),
        delete_child(action) {
            removed.push(actionKey(action));
            options = options.filter(child => actionKey(child.action) !== actionKey(action));
            return true;
        },
        get_submit_action: () => a,
        submit: allow => { if (!game.can_submit(allow)) return false; navigated.push('submit'); return true; },
        visit_parent: () => navigated.push('prev'),
        visit_child: () => { navigated.push('next'); return true; },
        suggest_action: () => a, get_current_present: () => ({ c: false, t: 1 }),
        get_historical_actions: () => [], get_comments: () => [], get_metadata: () => ({}),
        get_match_status: () => '', can_undo: () => true, can_redo: () => true,
        has_parent: () => true, can_submit: allow => allow || !game.currently_check(), currently_check: () => false,
        discard_preview() {}, undo() {}, delete() {}, get_current_checks: () => [],
        get_board_size: () => ({ x: 8, y: 8 }), get_current_boards: () => [],
        get_current_timeline_status: () => ({ mandatory_timelines: [] }),
        get_cached_moves: () => [], get_phantom_boards_and_checks: () => ({ boards: [], checks: [] }),
        show_pgn: () => options.map(child => child.pgn).join(' '),
    };
    const self = { postMessage: message => messages.push(structuredClone(message)) };
    const engine = { get_version: () => 'test',
        from_pgn(pgn, { allowIllegalHistory: allowIllegal = false, initialPositionOnly: initialOnly = false } = {}) {
            loads.push({ pgn, allowIllegal, initialOnly });
            // The setup-only candidate has its own lifetime, separate from the
            // active game and the fully prepared import candidate.
            if (initialOnly) return pgn === 'invalid' ? { success: false } : {
                success: true, game: { get_board_size: game.get_board_size,
                    get_current_boards: game.get_current_boards, delete() {} }
            };
            return pgn === 'invalid' || (pgn === 'illegal' && !allowIllegal)
                ? { success: false, message: 'Invalid PGN or illegal history' } : { success: true, game };
        } };
    const source = (await readFile(new URL('../scripts/worker.js', import.meta.url), 'utf8')).replace(/^import .*;\n/gm, '');
    vm.runInNewContext(source, { self, PendingBranches, actionKey, hashAction: actionKey,
        hashActionList: JSON.stringify, createModule: () => Promise.resolve(engine) });
    await Promise.resolve();
    self.game = game;
    const send = (type, data = {}) => self.onmessage({ data: { type, positionId: 0, ...data } });
    const last = type => messages.filter(message => message.type === type).at(-1);
    return { send, last, messages, removed, navigated, game, loads };
}

test('cancel, restore-all and stale confirmations leave the tree intact', async () => {
    const f = await workerFixture();
    f.send('toggle_deletion', { action: a });
    f.send('prev');
    assert.equal(f.last('confirm_deletions').actions[0], 'e4');
    f.send('cancel_deletions', { id: f.last('confirm_deletions').id });
    assert.equal(f.last('pending_deletions').count, 1);
    assert.deepEqual(f.removed, []);
    assert.deepEqual(f.navigated, []);
    f.send('prev');
    const id = f.last('confirm_deletions').id;
    f.send('restore_all');
    f.send('accept_deletions', { id });
    assert.equal(f.last('pending_deletions').count, 0);
    assert.deepEqual(f.removed, []);
    assert.deepEqual(f.navigated, []);
    f.send('toggle_deletion', { action: a, positionId: 99 });
    assert.equal(f.last('pending_deletions').count, 0);
});

test('next rejects pending/missing children and confirmation deletes before navigation', async () => {
    const f = await workerFixture();
    f.send('toggle_deletion', { action: a });
    f.send('next', { action: a });
    f.send('next', { action: move(0) });
    assert.equal(f.last('confirm_deletions'), undefined);
    f.send('next', { action: b });
    f.send('accept_deletions', { id: f.last('confirm_deletions').id });
    assert.deepEqual(f.removed, [actionKey(a)]);
    assert.deepEqual(f.navigated, ['next']);
});

test('submit preserves the matching original branch and confirms only other deletions', async () => {
    const f = await workerFixture();
    f.send('toggle_deletion', { action: a });
    f.send('toggle_deletion', { action: b });
    f.send('submit');
    assert.deepEqual(f.last('confirm_deletions').actions, ['d4']);
    f.send('accept_deletions', { id: f.last('confirm_deletions').id });
    assert.deepEqual(f.removed, [actionKey(b)]);
    assert.deepEqual(f.navigated, ['submit']);
    const only = await workerFixture();
    only.send('toggle_deletion', { action: a });
    only.send('submit');
    assert.equal(only.last('confirm_deletions'), undefined);
    assert.deepEqual(only.removed, []);
    assert.deepEqual(only.navigated, ['submit']);
});

test('export opening waits for confirmation; direct confirmation needs no popup', async () => {
    const f = await workerFixture();
    f.send('toggle_deletion', { action: a });
    f.send('open_export');
    assert.equal(f.last('open_export'), undefined);
    f.send('accept_deletions', { id: f.last('confirm_deletions').id });
    assert(f.last('open_export'));
    f.send('export', { requestId: 12 });
    assert.equal(f.last('update_pgn').pgn, 'd4');
    const direct = await workerFixture();
    direct.send('toggle_deletion', { action: a });
    direct.send('commit_deletions');
    assert.equal(direct.last('confirm_deletions'), undefined);
    assert.deepEqual(direct.removed, [actionKey(a)]);
});

test('failed load preserves pending changes; successful load clears them without deletion', async () => {
    const f = await workerFixture();
    f.send('toggle_deletion', { action: a });
    f.send('load', { pgn: 'invalid' });
    assert.equal(f.last('pending_deletions').count, 1);
    f.send('open_export');
    assert.equal(f.last('confirm_deletions').actions.length, 1);
    f.send('load', { pgn: 'valid' });
    assert.equal(f.last('pending_deletions').count, 0);
    assert.deepEqual(f.removed, []);
});


test('loading forwards explicit permission and rejects illegal history without replacing the game', async () => {
    const f = await workerFixture();
    let deleted = false;
    f.game.delete = () => { deleted = true; };
    f.send('toggle_deletion', { action: a });
    f.send('load', { pgn: 'illegal' });
    assert.equal(f.loads.at(-1).allowIllegal, false);
    assert(f.last('alert'));
    assert.equal(deleted, false);
    assert.equal(f.last('pending_deletions').count, 1);
    f.send('update_settings', { settings: { allowSubmitWithChecks: true } });
    f.send('load', { pgn: 'illegal' });
    assert.equal(f.loads.at(-1).allowIllegal, true);
    assert.equal(deleted, true);
    assert.equal(f.last('pending_deletions').count, 0);
});

test('Next requires permission before touching drafts or pending deletions', async () => {
    const f = await workerFixture();
    const options = [{ action: a, pgn: 'a3', is_illegal: true }, { action: b, pgn: 'd4', is_illegal: false }];
    f.game.get_child_actions = () => options;
    let discarded = 0;
    f.game.discard_preview = () => { discarded++; };
    f.send('toggle_deletion', { action: b });
    f.send('next', { action: a, is_illegal: false }); // UI data cannot override the engine flag.
    assert.match(f.last('alert').message, /Allow submit with checks/);
    assert.equal(f.last('confirm_deletions'), undefined);
    assert.deepEqual(f.navigated, []);
    assert.deepEqual(f.removed, []);
    assert.equal(discarded, 0);
    assert.equal(f.last('pending_deletions').count, 1);
    f.send('update_settings', { settings: { allowSubmitWithChecks: true } });
    f.send('next', { action: a });
    const confirmation = f.last('confirm_deletions');
    assert(confirmation);
    // Permission is rechecked when a pending confirmation is accepted.
    f.send('update_settings', { settings: { allowSubmitWithChecks: false } });
    f.send('accept_deletions', { id: confirmation.id });
    assert.deepEqual(f.navigated, []);
    assert.deepEqual(f.removed, []);
    f.send('update_settings', { settings: { allowSubmitWithChecks: true } });
    f.send('next', { action: a });
    f.send('accept_deletions', { id: f.last('confirm_deletions').id });
    assert.deepEqual(f.navigated, ['next']);
    assert.deepEqual(f.removed, [actionKey(b)]);
});

test('illegal history locks permission, disables Hint, and unlocks without restoring a preference', async () => {
    const f = await workerFixture();
    let illegal = false;
    f.game.has_illegal_history = () => illegal;
    f.game.get_child_actions = () => [{ action: a, pgn: 'a3', is_illegal: true }];
    f.game.visit_child = () => { illegal = true; return true; };
    f.game.visit_parent = () => { illegal = false; };
    f.game.suggest_action = () => illegal ? null : a;
    f.game.get_match_status = () => illegal ? "White's Move (Illegal History)" : "White's Move";
    f.send('update_settings', { settings: { allowSubmitWithChecks: true } });
    f.send('next', { action: a });
    assert.equal(f.last('update_buttons').allowSubmitWithChecksLocked, true);
    assert.equal(f.last('update_buttons').hint, false);
    assert.equal(f.last('update_hud_status').hudTitle, "White's Move (Illegal History)");
    assert.equal(f.last('update_select').options[0].is_illegal, true);
    f.send('update_settings', { settings: { allowSubmitWithChecks: false } });
    assert.equal(f.last('update_buttons').allowSubmitWithChecks, true);
    f.send('prev', { positionId: 1 });
    assert.equal(f.last('update_buttons').allowSubmitWithChecksLocked, false);
    assert.equal(f.last('update_buttons').allowSubmitWithChecks, true);
    f.send('update_settings', { settings: { allowSubmitWithChecks: false } });
    assert.equal(f.last('update_buttons').allowSubmitWithChecks, false);
    f.send('next', { positionId: 2, action: a });
    assert.match(f.last('alert').message, /illegal history/);
    assert.equal(illegal, false);
});

test('manual submission preserves its ordinary unresolved-check guard', async () => {
    const f = await workerFixture();
    f.game.currently_check = () => true;
    f.send('update_settings', { settings: { allowSubmitWithChecks: false } });
    assert.equal(f.last('update_buttons').submit, false);
    f.send('submit');
    assert.deepEqual(f.navigated, []);
    f.send('update_settings', { settings: { allowSubmitWithChecks: true } });
    assert.equal(f.last('update_buttons').submit, true);
    f.send('submit');
    assert.deepEqual(f.navigated, ['submit']);
});


test('import results are inline and prepared games only replace the current game after acceptance', async () => {
    const f = await workerFixture();
    let deleted = 0;
    f.game.delete = () => deleted++;
    f.send('toggle_deletion', { action: b });
    f.send('load', { pgn: 'invalid', requestId: 10 });
    assert.equal(f.last('load_result').success, false);
    assert.equal(f.last('load_result').requestId, 10);
    assert.equal(f.last('alert'), undefined);
    assert.equal(deleted, 0);
    f.send('load', { pgn: 'valid', requestId: 11 });
    assert.equal(f.last('load_result').success, true);
    assert.equal(deleted, 0);
    assert.equal(f.last('pending_deletions').count, 1);
    f.send('cancel_load', { requestId: 11 });
    assert.equal(deleted, 1); // Only the prepared candidate is disposed.
    f.send('commit_load', { requestId: 11 });
    assert.equal(f.last('load_committed'), undefined);
    f.send('load', { pgn: 'valid', requestId: 12 });
    f.send('commit_load', { requestId: 11 });
    assert.equal(f.last('load_committed'), undefined);
    f.send('commit_load', { requestId: 12 });
    assert.equal(f.last('load_committed').requestId, 12);
    assert.equal(f.last('pending_deletions').count, 0);
});

import { PendingBranches, actionKey } from './branch-deletion.js';
import createModule from '../wasm/engine.js';
import { parse_FEN } from './parse.js';
import { hashAction, hashActionList } from './hash.js';

function nextTurn(pos) {
    if (pos.c) {
        return { ...pos, t: pos.t + 1, c: false };
    } else {
        return { ...pos, t: pos.t, c: true };
    }
}

createModule().then((engine) => {
    self.engine = engine;
    self.game = null;
    let pendingHint = null;
    let nodeMoveStatus = null;
    let previewMove = null;
    let previewId = null;
    const pendingBranches = new PendingBranches();
    let positionId = 0;
    let confirmationId = 0;
    let confirmation = null;
    let pendingLoad = null;

    function children() {
        let flags = engine.SHOW_CAPTURE | engine.SHOW_PROMOTION;
        if (self.settings.useShortMoveNotation) flags |= engine.SHOW_SHORT;
        return self.game.get_child_actions(flags);
    }

    function commitBranches(plan) {
        // plan() has already validated and deduplicated all direct children.
        for (const child of plan) {
            if (!self.game.delete_child(child.action)) throw new Error('Could not delete the continuation.');
        }
        pendingBranches.clear();
        previewMove = previewId = null;
        self.game.discard_preview();
        prepareHint(true);
        updateSelect(true);
        updateButtons();
        updateHudStatus();
        viewGame();
    }

    function guardTransition(data) {
        if (data.positionId !== undefined && data.positionId !== positionId) return false;
        let submitted = null;
        if (data.type === 'next' && (!data.action || pendingBranches.has(data.action)
            || !self.game.has_child(data.action))) return false;
        if (data.type === 'next' && !self.settings.allowSubmitWithChecks) {
            const destination = children().find(child => actionKey(child.action) === actionKey(data.action));
            if (destination?.is_illegal) {
                self.postMessage({ type: 'alert', message: 'This continuation contains illegal history. Enable “Allow submit with checks” in Settings to view it.' });
                return false;
            }
        }
        if (data.type === 'prev' && !self.game.has_parent()) return false;
        if (data.type === 'submit') {
            submitted = self.game.get_submit_action();
            if (!submitted || !self.game.can_submit(self.settings.allowSubmitWithChecks)) return false;
        }
        if (!pendingBranches.keys.size) return true;
        const plan = pendingBranches.plan(children(), submitted);
        if (plan.length && !data.confirmed) {
            confirmation = { id: ++confirmationId, data };
            self.postMessage({ type: 'confirm_deletions', id: confirmation.id,
                operation: data.type, actions: plan.map(child => child.pgn) });
            return false;
        }
        if (pendingBranches.keys.size) commitBranches(plan);
        return true;
    }

    function prepareHint(preserveStatus = false) {
        pendingHint = self.game ? self.game.suggest_action() : null;
        if (self.game && !preserveStatus) {
            nodeMoveStatus = self.game.get_current_present().c ? "Black's Move" : "White's Move";
        }
    }
    self.has_children;
    self.memorizedActions = {};

    // Settings defaults
    self.settings = {
        allowSubmitWithChecks: false,
        showMovablePieces: false,
        useShortMoveNotation: true,
        exportMate: true,
        exportShortNotation: false,
        exportRelativeNotation: false,
        exportOutcome: false,
        exportStandardMetadataOnly: false,
        exportCompleteGameTree: true,
    };

    // Get and post the engine version
    const version = self.engine.get_version();
    self.postMessage({ type: 'engine_version', version: version });

    self.postMessage({ type: 'ready' });

    function replaceGame(next) {
        if (self.game) self.game.delete();
        self.game = next;
        pendingBranches.clear();
        previewMove = previewId = null;
        positionId++;
        confirmation = null;
    }

    function loadGame(pgn, requestId) {
        if (pendingLoad) pendingLoad.game.delete();
        pendingLoad = null;
        // Resolve setup without replaying history or checking royal exposure.
        // Post it before entering the synchronous full import, so the main
        // thread can prepare every surface LOD while this worker is occupied.
        const initial = self.engine.from_pgn(pgn, { allowIllegalHistory: true, initialPositionOnly: true });
        if (initial.success) {
            try {
                const size = initial.game.get_board_size();
                const boards = initial.game.get_current_boards();
                for (const board of boards) {
                    if (board.fen) board.parsed = parse_FEN(board.fen, size.x, size.y);
                }
                self.postMessage({ type: 'initial_boards', requestId, data: { size, boards } });
            } finally {
                initial.game.delete();
            }
        }
        const result = self.engine.from_pgn(pgn, { allowIllegalHistory: self.settings.allowSubmitWithChecks });
        if (requestId !== undefined && requestId !== null) {
            if (result.success) pendingLoad = { requestId, game: result.game };
            self.postMessage({ type: 'load_result', requestId, success: result.success,
                diagnostic: result.diagnostic, message: result.message });
            return false;
        }
        // Startup loads have no import editor to receive diagnostics.
        if (!result.success) {
            self.postMessage({ type: 'alert', message: result.message });
            return false;
        }
        replaceGame(result.game);
        return true;
    }

    function refreshLoadedGame() {
        prepareHint();
        updateSelect();
        updateButtons();
        updateHudStatus();
        viewGame(true);
    }

    function viewGame(positionChanged = false) {
        if (self.game === null) {
            self.postMessage({ type: 'alert', message: 'No game loaded.' });
            return;
        }
        let size = self.game.get_board_size();
        let boards = self.game.get_current_boards();
        let present = self.game.get_current_present();
        let timeline_status = self.game.get_current_timeline_status();
        let focus = timeline_status.mandatory_timelines.map((l) => ({
            l: l,
            t: present.t,
            c: present.c,
        }));
        let highlights = [];
        // check arrows
        const checking = self.game.currently_check();
        if (checking) {
            const checks = self.game.get_current_checks();
            const checkArrows = checks.map((mv) => ({
                from: {
                    l: mv.from.l,
                    t: mv.from.t,
                    y: mv.from.y,
                    x: mv.from.x,
                    c: !present.c,
                },
                to: {
                    l: mv.to.l,
                    t: mv.to.t,
                    y: mv.to.y,
                    x: mv.to.x,
                    c: !present.c,
                },
            }));
            highlights.push({
                color: '--highlight-check',
                arrows: checkArrows,
            });
        }
        // piece moves
        const cachedMoves = self.game.get_cached_moves();
        if (previewMove) cachedMoves.push(previewMove);
        let historyRaw = [
            ...self.game.get_historical_actions(),
            { action: cachedMoves },
        ];
        let whiteMoveCoordinates = [];
        let blackMoveCoordinates = [];
        let whiteMoveArrows = [];
        let blackMoveArrows = [];

        let history = historyRaw.map((item, index) => ({
            ...item,
            c: !!((historyRaw.length - index) % 2) === present.c,
        }));
        for (let entry of history) {
            for (let move of entry.action) {
                let from = { ...move.from, c: entry.c };
                let to = { ...move.to, c: entry.c };
                if (from.l === to.l && from.t === to.t) {
                    if (entry.c) {
                        blackMoveCoordinates.push(nextTurn(from));
                        blackMoveCoordinates.push(nextTurn(to));
                    } else {
                        whiteMoveCoordinates.push(nextTurn(from));
                        whiteMoveCoordinates.push(nextTurn(to));
                    }
                } else {
                    if (entry.c) {
                        blackMoveArrows.push({ from, to });
                    } else {
                        whiteMoveArrows.push({ from, to });
                    }
                }
            }
        }
        highlights.push({
            color: '--highlight-white-move',
            coordinates: whiteMoveCoordinates,
        });
        highlights.push({
            color: '--highlight-white-move-arrow',
            arrows: whiteMoveArrows,
        });
        highlights.push({
            color: '--highlight-black-move',
            coordinates: blackMoveCoordinates,
        });
        highlights.push({
            color: '--highlight-black-move-arrow',
            arrows: blackMoveArrows,
        });
        if(self.settings.showMovablePieces) {
            let movablePieces = self.game.get_movable_pieces();
            for (let p of movablePieces) {
                p.c = present.c;
            }
            highlights.push({
                color: '--highlight-movable-piece',
                coordinates: movablePieces,
            });
        }
        
        for (let board of boards) {
            if (board.fen) {
                board.parsed = parse_FEN(board.fen, size.x, size.y);
            }
        }

        let phantomData = self.game.get_phantom_boards_and_checks();
        let phantom = phantomData.boards;
        let phantomChecks = phantomData.checks;
        for (let board of phantom) {
            if (board.fen) {
                board.parsed = parse_FEN(board.fen, size.x, size.y);
            }
        }

        let data = {boards, present, focus, highlights, size, phantom, phantomChecks};
        if(checking) {
            data.fade = 0.8;
        }
        self.postMessage({
            type: 'data',
            data: data,
            positionChanged,
            previewId,
        });
    }

    function genMoveIfPlayable(pos) {
        if (self.game === null) {
            self.postMessage({ type: 'alert', message: 'No game loaded.' });
            return [];
        }
        return self.game.gen_move_if_playable(pos);
    }

    function updateSelect(preserveSelection = false, hintAdded = false) {
        const options = children().map(child => ({ ...child, deleted: pendingBranches.has(child.action) }));
        self.has_children = options.some(child => !child.deleted);
        self.postMessage({ type: 'pending_deletions', count: pendingBranches.keys.size });
        let action = readMemorizedAction();
        if (action) {
            let found = options.findIndex(child => {
                return hashAction(child.action) === hashAction(action);
            });
            self.postMessage({
                type: 'update_select',
                preserveSelection,
                hintAdded,
                options,
                positionId,
                selectedIndex: found
            });
        } else {
            self.postMessage({
                type: 'update_select',
                preserveSelection,
                hintAdded,
                options,
                positionId
            });
        }
    }

    function updateButtons() {
        if (self.game === null) {
            return;
        }
        self.postMessage({
            type: 'update_buttons',
            allowSubmitWithChecks: self.settings.allowSubmitWithChecks,
            allowSubmitWithChecksLocked: self.game.has_illegal_history(),
            hint: pendingHint !== null,
            undo: self.game.can_undo(),
            redo: self.game.can_redo(),
            prev: self.game.has_parent(),
            next: self.has_children,
            submit: self.game.can_submit(self.settings.allowSubmitWithChecks),
        });
    }

    function updateHudStatus() {
        if (self.game === null) {
            return;
        }
        // Get match status and comments
        const matchStatus = !self.game.has_illegal_history() && pendingHint !== null
            ? nodeMoveStatus
            : self.game.get_match_status();
        const comments = self.game.get_comments();
        const hudText =
            comments && comments.length > 0
                ? comments[comments.length - 1]
                : null;

        self.postMessage({
            type: 'update_hud_status',
            hudTitle: matchStatus,
            metadata: self.game.get_metadata(),
            hudText: hudText,
        });
    }

    function updateMemorizedAction() {
        let historyRaw = self.game.get_historical_actions();
        let history = historyRaw.map((item) => item.action);
        let action = history.pop();
        let hash = hashActionList(history);
        self.memorizedActions[hash] = action;
    }

    function readMemorizedAction() {
        let historyRaw = self.game.get_historical_actions();
        let history = historyRaw.map((item) => item.action);
        let hash = hashActionList(history);
        return self.memorizedActions[hash];
    }

    self.onmessage = (e) => {
        let data = e.data;
        if (data.type === 'cancel_load') {
            if (pendingLoad && pendingLoad.requestId === data.requestId) {
                pendingLoad.game.delete();
                pendingLoad = null;
            }
            return;
        }
        if (data.type === 'commit_load') {
            if (!pendingLoad || pendingLoad.requestId !== data.requestId) return;
            const next = pendingLoad.game;
            pendingLoad = null;
            replaceGame(next);
            refreshLoadedGame();
            self.postMessage({ type: 'load_committed', requestId: data.requestId });
            return;
        }
        if (data.type === 'cancel_deletions') {
            if (confirmation?.id === data.id) confirmation = null;
            return;
        }
        if (data.type === 'accept_deletions') {
            if (!confirmation || confirmation.id !== data.id) return;
            data = { ...confirmation.data, confirmed: true };
            confirmation = null;
        }
        if (['prev', 'next', 'submit', 'open_export', 'export'].includes(data.type)) {
            if (!guardTransition(data)) return;
        }
        if (['load', 'apply_move', 'undo', 'redo', 'submit', 'prev', 'next', 'hint',
            'toggle_deletion', 'restore_all', 'commit_deletions'].includes(data.type)) confirmation = null;
        if (data.type === 'toggle_deletion' || data.type === 'restore_all' || data.type === 'commit_deletions') {
            if (data.positionId !== positionId) return;
            if (data.type === 'toggle_deletion') pendingBranches.toggle(data.action, children());
            else if (data.type === 'restore_all') pendingBranches.clear();
            else commitBranches(pendingBranches.plan(children()));
            updateSelect(true);
            updateButtons();
            return;
        }
        if (data.type === 'open_export') {
            self.postMessage({ type: 'open_export' });
            return;
        }
        if (['apply_move', 'undo', 'redo', 'submit', 'prev', 'next', 'hint'].includes(data.type)) {
            self.game?.discard_preview();
            previewMove = previewId = null;
        }
        if (data.type === 'load') {
            if (!loadGame(data.pgn, data.requestId)) return;
            refreshLoadedGame();
        } else if (data.type === 'view') {
            viewGame();
        } else if (data.type === 'apply_move') {
            self.game.apply_move({ from: data.from, to: data.to });
            updateButtons();
            viewGame(true);
        } else if (data.type === 'preview_move') {
            previewMove = { from: data.from, to: data.to };
            previewId = data.requestId;
            if (!self.game.preview_move(previewMove)) previewMove = null;
            viewGame();
        } else if (data.type === 'discard_preview') {
            if (previewId === data.requestId) {
                self.game.discard_preview();
                previewMove = previewId = null;
                updateButtons();
                viewGame();
            }
        } else if (data.type === 'gen_move_if_playable') {
            const moves = genMoveIfPlayable(data.pos);
            self.postMessage({ type: 'moves', moves: moves, requestId: data.requestId });
        } else if (data.type === 'submit') {
            if (!self.game.submit(self.settings.allowSubmitWithChecks)) return;
            positionId++;
            self.postMessage({ type: 'navigated' });
            prepareHint();
            updateSelect();
            updateMemorizedAction();
            updateButtons();
            updateHudStatus();
            viewGame(true);
        } else if (data.type === 'undo') {
            self.game.undo();
            updateButtons();
            viewGame(true);
        } else if (data.type === 'redo') {
            self.game.redo();
            updateButtons();
            viewGame(true);
        } else if (data.type === 'prev') {
            self.game.visit_parent();
            positionId++;
            self.postMessage({ type: 'navigated' });
            prepareHint();
            updateSelect();
            updateButtons();
            updateHudStatus();
            viewGame(true);
        } else if (data.type === 'next') {
            if (!self.game.visit_child(data.action)) return;
            positionId++;
            self.postMessage({ type: 'navigated' });
            prepareHint();
            updateSelect();
            updateMemorizedAction();
            updateButtons();
            updateHudStatus();
            viewGame(true);
        } else if (data.type === 'hint') {
            if (pendingHint !== null && self.game.add_child(pendingHint)) {
                prepareHint(true);
                updateSelect(true, true);
            }
            updateButtons();
        } else if (data.type === 'update_settings') {
            // Illegal history can only be entered with explicit permission.
            // Keep it enabled until navigation returns to a legal ancestor.
            if (self.game?.has_illegal_history() && data.settings?.allowSubmitWithChecks === false) {
                data = { ...data, settings: { ...data.settings, allowSubmitWithChecks: true } };
            }
            self.settings = Object.assign(
                self.settings || {},
                data.settings || {},
            );
            if (data.settings?.useShortMoveNotation !== undefined && self.game) updateSelect(true);
            // Update UI state if needed
            updateButtons();
        } else if (data.type === 'export') {
            let flags = engine.SHOW_CAPTURE | engine.SHOW_PROMOTION;
            if (self.settings.exportMate) {
                flags |= engine.SHOW_MATE;
            }
            if (self.settings.exportShortNotation) {
                flags |= engine.SHOW_SHORT;
            }
            if (self.settings.exportRelativeNotation) {
                flags |= engine.SHOW_RELATIVE;
            }
            if (self.settings.exportOutcome) {
                flags |= engine.SHOW_OUTCOME;
            }
            let pgn = self.game.show_pgn(
                flags,
                self.settings.exportCompleteGameTree !== false,
                self.settings.exportStandardMetadataOnly === true,
            );
            self.postMessage({ type: 'update_pgn', pgn: pgn, requestId: data.requestId });
        } else if (data.type === 'update_comment') {
            let comments = data.comments || [];
            if (comments.length > 0) {
                if (data.comment && data.comment !== '') {
                    comments[comments.length - 1] = data.comment;
                } else {
                    comments.pop();
                }
            } else if (data.comment && data.comment !== '') {
                comments.push(data.comment);
            }
            self.game.set_comments(comments);
            updateHudStatus();
        }
    };
});

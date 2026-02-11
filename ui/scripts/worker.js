import createModule from '../wasm/engine.js';
import { parse_FEN } from './parse.js';

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
    self.has_children;

    // Settings defaults
    self.settings = {
        allowSubmitWithChecks: false,
        showMovablePieces: false,
    };

    // Get and post the engine version
    const version = self.engine.get_version();
    self.postMessage({ type: 'engine_version', version: version });

    self.postMessage({ type: 'ready' });

    function loadGame(pgn) {
        let g0 = self.engine.from_pgn(pgn);
        if (!g0.success) {
            self.postMessage({ type: 'alert', message: g0.message });
        } else {
            if (self.game) {
                self.game.delete();
            }
            self.game = g0.game;
        }
    }

    function viewGame() {
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
        let historyRaw = [
            ...self.game.get_historical_actions(),
            { action: self.game.get_cached_moves() },
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
        });
    }

    function genMoveIfPlayable(pos) {
        if (self.game === null) {
            self.postMessage({ type: 'alert', message: 'No game loaded.' });
            return [];
        }
        return self.game.gen_move_if_playable(pos);
    }

    function updateSelect() {
        let children = self.game.get_child_actions();
        self.has_children = children.length > 0;
        self.postMessage({
            type: 'update_select',
            options: children,
        });
    }

    function updateButtons() {
        self.postMessage({
            type: 'update_buttons',
            undo: self.game.can_undo(),
            redo: self.game.can_redo(),
            prev: self.game.has_parent(),
            next: self.has_children,
            submit: (() => {
                if (self.settings.allowSubmitWithChecks) {
                    return self.game.can_submit();
                } else {
                    return (
                        self.game.can_submit() && !self.game.currently_check()
                    );
                }
            })(),
        });
    }

    function updateHudStatus() {
        // Get match status and comments
        const matchStatus = self.game.get_match_status();
        const comments = self.game.get_comments();
        const hudText =
            comments && comments.length > 0
                ? comments[comments.length - 1]
                : null;

        self.postMessage({
            type: 'update_hud_status',
            hudTitle: matchStatus,
            hudText: hudText,
        });
    }

    self.onmessage = (e) => {
        const data = e.data;
        if (data.type === 'load') {
            loadGame(data.pgn);
            updateSelect();
            updateButtons();
            updateHudStatus();
            viewGame();
        } else if (data.type === 'view') {
            viewGame();
        } else if (data.type === 'apply_move') {
            self.game.apply_move({ from: data.from, to: data.to });
            updateButtons();
            viewGame();
        } else if (data.type === 'gen_move_if_playable') {
            const moves = genMoveIfPlayable(data.pos);
            self.postMessage({ type: 'moves', moves: moves });
        } else if (data.type === 'submit') {
            self.game.submit();
            updateSelect();
            updateButtons();
            updateHudStatus();
            viewGame();
        } else if (data.type === 'undo') {
            self.game.undo();
            updateButtons();
            viewGame();
        } else if (data.type === 'redo') {
            self.game.redo();
            updateButtons();
            viewGame();
        } else if (data.type === 'prev') {
            self.game.visit_parent();
            updateSelect();
            updateButtons();
            updateHudStatus();
            viewGame();
        } else if (data.type === 'next') {
            self.game.visit_child(data.action);
            updateSelect();
            updateButtons();
            updateHudStatus();
            viewGame();
        } else if (data.type === 'hint') {
            self.game.suggest_action();
            updateSelect();
            updateButtons();
        } else if (data.type === 'update_settings') {
            // Merge incoming settings
            self.settings = Object.assign(
                self.settings || {},
                data.settings || {},
            );
            // Update UI state if needed
            updateButtons();
        } else if (data.type === 'export') {
            let pgn = self.game.show_pgn();
            self.postMessage({ type: 'update_pgn', pgn: pgn });
        }
    };
});

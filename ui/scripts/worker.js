import createModule from '../wasm/engine.js';

createModule().then((engine) => {
    self.engine = engine;
    self.game = null;
    self.has_children;

    self.postMessage({type: 'ready'});

    function loadGame(pgn) {
        let g0 = self.engine.from_pgn(pgn);
        if(!g0.success)
        {
            self.postMessage({type: 'alert', message: g0.message});
        }
        else
        {
            if(self.game)
            {
                self.game.delete();
            }
            self.game = g0.game;
        }
    }

    function viewGame() {
        if(self.game === null)
        {
            self.postMessage({type: 'alert', message: 'No game loaded.'});
            return;
        }
        let boards = self.game.get_current_boards();
        let present = self.game.get_current_present();
        let timeline_status = self.game.get_current_timeline_status();
        let focus = timeline_status.mandatory_timelines.map(l => {
            return {
                l: l,
                t: present.t,
                c: present.c
            }
        });
        self.postMessage({type: 'data', data: {
            boards: boards,
            present: present,
            focus: focus
        }});
    }

    function genMoveIfPlayable(pos) {
        if(self.game === null)
        {
            self.postMessage({type: 'alert', message: 'No game loaded.'});
            return [];
        }
        return self.game.gen_move_if_playable(pos);
    }

    function updateSelect() {
        let children = self.game.get_child_moves();
        self.has_children = children.length > 0;
        self.postMessage({
            type: 'update_select',
            options: children
        });
    }

    function updateButtons() {
        self.postMessage({
            type: 'update_buttons',
            undo: self.game.can_undo(),
            redo: self.game.can_redo(),
            prev: self.game.has_parent(),
            next: self.has_children,
            submit: self.game.can_submit()
        });
    }

    function updateHudStatus() {
        // Get match status and comments
        const matchStatus = self.game.get_match_status();
        const comments = self.game.get_comments();
        const hudText = comments && comments.length > 0 ? comments[comments.length - 1] : null;
        
        self.postMessage({
            type: 'update_hud_status',
            hudTitle: matchStatus,
            hudText: hudText
        });
    }


    self.onmessage = (e) => {
        const data = e.data;
        if (data.type === 'load')
        {
            loadGame(data.pgn);
            updateSelect();
            updateButtons();
            updateHudStatus();
            viewGame();
        }
        else if (data.type === 'view')
        {
            viewGame();
        }
        else if (data.type === 'apply_move')
        {
            self.game.apply_move({from: data.from, to: data.to});
            updateButtons();
            viewGame();
        }
        else if (data.type === 'gen_move_if_playable')
        {
            const moves = genMoveIfPlayable(data.pos);
            self.postMessage({type: 'moves', moves: moves});
        }
        else if (data.type === 'submit')
        {
            self.game.submit();
            updateSelect();
            updateButtons();
            updateHudStatus();
            viewGame();
        }
        else if (data.type === 'undo')
        {
            self.game.undo();
            updateButtons();
            viewGame();
        }
        else if (data.type === 'redo')
        {
            self.game.redo();
            updateButtons();
            viewGame();
        }
        else if (data.type === 'prev')
        {
            self.game.visit_parent();
            updateSelect();
            updateButtons();
            updateHudStatus();
            viewGame();
        }
        else if (data.type === 'next')
        {
            self.game.visit_child(data.action);
            updateSelect();
            updateButtons();
            updateHudStatus();
            viewGame();
        }
        else if (data.type === 'hint')
        {
            self.game.suggest_action();
            updateSelect();
            updateButtons();
        }
        else if (data.type === 'export')
        {
            let pgn = self.game.show_pgn();
            self.postMessage({type: 'update_pgn', pgn: pgn});
        }
    };
});

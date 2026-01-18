import {createModule} from '../wasm/engine.js';
import {parse_FEN} from './parse.js';

createModule().then((engine) => {
    self.engine = engine;
    self.postMessage({type: 'ready'});
    self.game = null;

    function load_game(pgn) {
        let g0 = self.engine.from_pgn(pgn);
        if(!g0.success)
        {
            self.postMessage({type: 'alert', message: g0.message});
        }
        else
        {
            self.game = g0.game;
        }
    }

    function view_game() {
        if(self.game === null)
        {
            self.postMessage({type: 'alert', message: 'No game loaded.'});
            return;
        }
        let fen = self.engine.get_current_boards(self.game);
        //self.postMessage({type: 'board', board: board_array, fen: fen});
    }

    self.onmessage = function(e) {
        const data = e.data;
        if(data.type === 'load')
        {
            load_game(data.pgn);
        }
    };

    load_game('[Board "Standard"]');
});

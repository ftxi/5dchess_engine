import createModule from '../wasm/engine.js';
//import {parse_FEN} from './parse.js';

createModule().then((engine) => {
    self.engine = engine;
    self.game = null;
    console.log("Engine module loaded in worker.");

    self.postMessage({type: 'ready'});

    function load_game(pgn) {
        if(self.game)
        {
            self.game.delete();
            self.game = null;
        }
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
        let boards = self.game.get_current_boards();
        let present = self.game.get_current_present();
        self.postMessage({type: 'data', data: {
            boards: boards,
            present: present
        }});
    }

    self.onmessage = (e) => {
        const data = e.data;
        console.log("Received message of type: " + data.type);
        if(data.type === 'load')
        {
            load_game(data.pgn);
            view_game();
        }
        else if(data.type === 'view')
        {
            view_game();
        }
    };

    load_game('[Board "Standard"]');
});

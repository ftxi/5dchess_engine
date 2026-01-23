// main.js

import { UI } from 'ui';
import ChessBoardCanvasBase from 'draw';


let worker = new Worker(window.worker_path, { type: 'module' });

// State management for move selection
let clicking = false;
let clicked_pos = null;
let generated_moves = [];
let present_c;

class ChessBoardCanvas extends ChessBoardCanvasBase {
    onClickSquare(l, t, c, x, y) {
        handle_click({l: l, t: t, c: c, x: x, y: y});
    }
    
    onRightClickSquare() {
        deselect();
    }
}

window.chessBoardCanvas = new ChessBoardCanvas('canvas');

function handle_click(pos) {
    if (clicking) {
        return;
    }
    if(pos.c !== present_c) {
        if (clicked_pos !== null) {
            deselect();
        }
        return; // Ignore clicks on other colors
    }
    if (clicked_pos !== null) {
        let from = clicked_pos;
        let to = pos;
        clicked_pos = null;
        if (generated_moves.some(mv => mv.l === to.l && mv.t === to.t && mv.x === to.x && mv.y === to.y)) {
            worker.postMessage({type: 'apply_move', from: from, to: to});
            generated_moves = [];
        }
    } else {
        clicking = true;
        worker.postMessage({type: 'gen_move_if_playable', pos: pos});
        clicked_pos = pos;
    }
}

function deselect() {
    clicked_pos = null;
    generated_moves = [];
    worker.postMessage({type: 'view'});
}

function addHighlight(data, color, field, values) {
  if (!Array.isArray(data.highlights)) {
    data.highlights = [];
  }
  let colorBlock = data.highlights.find(h => h.color === color);
  if (!colorBlock) {
    data.highlights.push({
      color,
      [field]: [...values],
    });
  } else {
    if (!Array.isArray(colorBlock[field])) {
      colorBlock[field] = [];
    }
    colorBlock[field].push(...values);
  }
}


worker.onmessage = (e) => {
    const msg = e.data;
    if (msg.type === 'ready') {
        worker.postMessage({type: 'load', pgn: '[Board "Standard"]'});
    }
    else if (msg.type === 'alert') {
        alert('[WORKER] ' + msg.message);
    }
    else if (msg.type === 'data') {
        let data = msg.data;
        present_c = data.present.c;
        addHighlight(data, '#80cc3f', 'coordinates', generated_moves.map(q => ({l: q.l, t: q.t, x: q.x, y: q.y, c: present_c})));
        window.chessBoardCanvas.setData(data);
        clicking = false;
    }
    else if (msg.type === 'moves') {
        generated_moves = msg.moves;
        if (generated_moves.length === 0){
            clicked_pos = null;
            clicking = false;
        }
        else
        {
            worker.postMessage({type: 'view'});
        }
    }
    else if (msg.type === 'update_buttons')
    {
        console.log(msg);
        for(let key of ['undo', 'redo', 'prev', 'next', 'submit'])
        {
            if(msg[key])
            {
                UI.buttons.enable(key);
            }
            else
            {
                UI.buttons.disable(key);
            }
        }
    }
}

UI.buttons.setCallbacks({
    prev: () => {
        worker.postMessage({type: 'prev'});
    },
    next: () => {
        let action = UI.select.getSelectedValue();
        worker.postMessage({type: 'next', action: action});
    },
    undo: () => {
        worker.postMessage({type: 'undo'});
    },
    redo: () => {
        worker.postMessage({type: 'redo'});
    },
    submit: () => {
        worker.postMessage({type: 'submit'});
    },
    hint: () => { /* handle hint */ }
});


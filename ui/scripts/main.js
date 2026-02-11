// main.js

import { UI } from 'ui';
import ChessBoardCanvasBase from 'draw';


let worker = new Worker(window.worker_path, { type: 'module' });

// State management for move selection
let clicking = false;
let clickedPos = null;
let generatedMoves = [];
let presentC;
let nextOptions = [];
let showPhantom = true;

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
    if(pos.c !== presentC) {
        if (clickedPos !== null) {
            deselect();
        }
        return; // Ignore clicks on other colors
    }
    if (clickedPos !== null) {
        let from = clickedPos;
        let to = pos;
        clickedPos = null;
        if (generatedMoves.some(mv => mv.l === to.l && mv.t === to.t && mv.x === to.x && mv.y === to.y)) {
            worker.postMessage({type: 'apply_move', from: from, to: to});
            generatedMoves = [];
        } else {
            deselect();
        }
    } else {
        clicking = true;
        worker.postMessage({type: 'gen_move_if_playable', pos: pos});
        clickedPos = pos;
    }
}

function deselect() {
    clickedPos = null;
    generatedMoves = [];
    worker.postMessage({type: 'view'});
}

function disablePhantom() {
    showPhantom = false;
    UI.setHudLight(false);
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
        worker.postMessage({type: 'load', pgn: '[Board "Standard - Turn Zero"]'});
    }
    else if (msg.type === 'engine_version') {
        // Update the version in the Information popup and welcome popup
        UI.setVersionNumber(msg.version);
        console.log('Version', msg.version);
        // Show info popup on startup if enabled
        UI.showInfoPage();
    }
    else if (msg.type === 'alert') {
        alert('[WORKER] ' + msg.message);
    }
    else if (msg.type === 'data') {
        let data = msg.data;
        presentC = data.present.c;
        addHighlight(data, '--highlight-generated-move', 'coordinates', generatedMoves.map(q => ({l: q.l, t: q.t, x: q.x, y: q.y, c: presentC})));
        if(data.phantom && data.phantom.length > 0) {
            UI.setHudLight(true);
            if (showPhantom) {
                addHighlight(data, '--highlight-phantom-board', 'boards', data.phantom.map((b) => ({l: b.l, t: b.t, c: !presentC})));
                data.boards = data.boards.concat(data.phantom);
                addHighlight(data, '--highlight-check', 'arrows', data.phantomChecks.map((c) => ({
                    from: {...c.from, c: !presentC},
                    to: {...c.to, c: !presentC}
                })));
                data.fade = 0.5;
            }
        } else {
            UI.setHudLight(false);
        }
        window.chessBoardCanvas.setData(data);
        clicking = false;
    }
    else if (msg.type === 'moves') {
        generatedMoves = msg.moves;
        if (generatedMoves.length === 0){
            clickedPos = null;
            clicking = false;
        }
        else
        {
            worker.postMessage({type: 'view'});
        }
    }
    else if (msg.type === 'update_buttons')
    {
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
    else if (msg.type === 'update_select')
    {
        let options = msg.options.map(obj => obj.pgn);
        nextOptions = msg.options.map(obj => obj.action);
        UI.select.setOptions(options);
    }
    else if (msg.type === 'update_pgn')
    {
        UI.setExportData(msg.pgn);
    }
    else if (msg.type === 'update_hud_status')
    {
        UI.setHudTitle(msg.hudTitle);
        if (msg.hudText) {
            UI.setHudText(msg.hudText);
        }
        else {
            UI.setHudText('');
        }
    }
}

UI.buttons.setCallbacks({
    prev: () => {
        deselect();
        worker.postMessage({type: 'prev'});
        disablePhantom();
    },
    next: () => {
        deselect();
        let index = UI.select.getSelectedIndex();
        let action = nextOptions[index];
        worker.postMessage({type: 'next', action: action});
        disablePhantom();
    },
    undo: () => {
        deselect();
        worker.postMessage({type: 'undo'});
    },
    redo: () => {
        deselect();
        worker.postMessage({type: 'redo'});
    },
    submit: () => {
        deselect();
        worker.postMessage({type: 'submit'});
        disablePhantom();
    }
});

UI.setHintCallback(() => {
    worker.postMessage({type: 'hint'});
});

UI.setFocusCallback(() => {
    window.chessBoardCanvas.goToNextFocus();
});

UI.setImportCallback((data) => {
    worker.postMessage({type: 'load', pgn: data});
});

UI.setExportCallback(() => {
    worker.postMessage({type: 'export'});
});

UI.setCommentsEditCallback((text) => {
    worker.postMessage({ type: 'update_comment', comment: text });
});

// Unified settings handler: forward to worker and handle local UI changes
UI.setSettingsChangeCallback((settings) => {
    // Forward all settings updates to the worker
    worker.postMessage({ type: 'update_settings', settings: settings });

    // Handle local UI changes based on settings keys
    if (settings.theme !== undefined) {
        // Theme changed: reload canvas colors
        if (window.chessBoardCanvas && window.chessBoardCanvas.reloadColors) {
            window.chessBoardCanvas.reloadColors();
        }
    } else if (settings.debugWindow !== undefined) {
        // Toggle debug window visibility
        const dbg = document.querySelector('.debug-window');
        if (dbg) dbg.style.display = settings.debugWindow ? '' : 'none';
    } else if (settings.allowSubmitWithChecks !== undefined) {
        // No local action needed here; worker will use this
    } else if (settings.showMovablePieces !== undefined) {
        worker.postMessage({ type: 'view' });
    }
});

// Testing HUD light functions
UI.setHudLightCallback((isOn) => {
    showPhantom = isOn;
    worker.postMessage({ type: 'view' });
});

UI.setHudLight(false); // Initialize HUD light to off

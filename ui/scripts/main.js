import { chooseLOD } from 'piece';
console.log("LOD function imported:", chooseLOD(55));

import { ChessBoardCanvas } from 'draw';

window.chessBoardCanvas = new ChessBoardCanvas('canvas');
let worker = new Worker(window.worker_path, { type: 'module' });


worker.onmessage = (e) => {
    const msg = e.data;
    if (msg.type === 'ready') {
        worker.postMessage({type: 'load', pgn: '[Board "Standard"]'});
    }
    else if (msg.type === 'alert') {
        alert('[WORKER] ' + msg.message);
    }
    else if (msg.type === 'data') {
        window.chessBoardCanvas.setData(msg.data);
    }
    else {
        console.log('[WORKER] Unknown message type:', msg);
    }
}




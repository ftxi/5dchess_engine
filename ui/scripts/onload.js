'use strict';

window.onload = () => { 
    window.chessBoardCanvas = new ChessBoardCanvas('canvas', 'btnFocus');
    
    (async () => {
        window.chessBoardCanvas.setSvgImages(await loadAllSvgs());
        request_data();
    })();
}

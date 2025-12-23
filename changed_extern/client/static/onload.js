'use strict';

window.onload = () => 
{
    window.chessBoardCanvas = new ChessBoardCanvas('display', 'status', 'center-btn');
    
    (async () => {
        window.chessBoardCanvas.setSvgImages(await loadAllSvgs());
        request_data();
    })();

    document.getElementById('text-window').style.visibility = 'hidden';
    document.getElementById('toggle-text').addEventListener('click', () => {
        const textWindow = document.getElementById('text-window');
        const toggleButton = document.getElementById('toggle-text');
        
        if (textWindow.style.visibility === 'hidden') {
            textWindow.style.visibility = 'visible';
            toggleButton.classList.remove('toggle-text-inactive');
            toggleButton.classList.add('toggle-text-active');
        } else {
            textWindow.style.visibility = 'hidden';
            toggleButton.classList.remove('toggle-text-active');
            toggleButton.classList.add('toggle-text-inactive');
        }
    });
    document.getElementById('screenshot').addEventListener('click', () => {
        let canvasImage = document.getElementById('display').toDataURL('image/png');
        // this can be used to download any image from webpage to local disk
        let xhr = new XMLHttpRequest();
        xhr.responseType = 'blob';
        xhr.onload = function () {
            let a = document.createElement('a');
            a.href = window.URL.createObjectURL(xhr.response);
            function getTimestamp() {
                const now = new Date();
                return now.toISOString().replace(/\D/g, '').slice(0, 14);
            }
            a.download = `5dc_${getTimestamp()}.png`;
            a.style.display = 'none';
            document.body.appendChild(a);
            a.click();
            a.remove();
        };
        xhr.open('GET', canvasImage); // This is to download the canvas Image
        xhr.send();
    });

    const load_popup = document.getElementById("txt-popup");
    const load_btn = document.getElementById("load-btn");
    const close_load_btn = document.getElementById("close-popup");
    const load_popup_button = document.getElementById("load-popup");

    load_btn.onclick = () => {
        load_popup.style.display = "block";
    };

    close_load_btn.onclick = () => {
        load_popup.style.display = "none";
    };

    load_popup_button.onclick = request_load;

    const export_popup = document.getElementById("export-popup");
    const export_btn = document.getElementById("export-btn");
    const close_export_btn = document.getElementById("close-export-popup");

    export_btn.onclick = () => {
        export_popup.style.display = "block";
        request_pgn();
    };

    close_export_btn.onclick = () => {
        export_popup.style.display = "none";
    };

    // Perft button event listener
    const perft_btn = document.getElementById("perft-btn");
    const perft_depth_input = document.getElementById("perft-depth");
    const perft_timeout_input = document.getElementById("perft-timeout");
    const perft_dynamic_checkbox = document.getElementById("perft-dynamic");
    const perft_tt_checkbox = document.getElementById("perft-tt");
    const perft_parallel_checkbox = document.getElementById("perft-parallel");
    
    perft_btn.onclick = () => {
        const depth = parseInt(perft_depth_input.value) || 1;
        const timeout = parseInt(perft_timeout_input.value) || 0;
        const dynamic = perft_dynamic_checkbox.checked;
        const use_tt = perft_tt_checkbox.checked;
        const parallel = perft_parallel_checkbox.checked;
        request_perft(depth, timeout, dynamic, use_tt, parallel);
    };
    
    // Allow pressing Enter in the depth input to trigger perft
    perft_depth_input.addEventListener('keydown', (event) => {
        if (event.key === 'Enter') {
            const depth = parseInt(perft_depth_input.value) || 1;
            const timeout = parseInt(perft_timeout_input.value) || 0;
            const dynamic = perft_dynamic_checkbox.checked;
            const use_tt = perft_tt_checkbox.checked;
            const parallel = perft_parallel_checkbox.checked;
            request_perft(depth, timeout, dynamic, use_tt, parallel);
        }
    });

    const go_to_center = () => chessBoardCanvas.goToNextFocus();

    document.addEventListener("keydown", function(event) {
        if (event.key === "Tab" || event.key === " ") 
        {
            go_to_center();
        } 
        else if (event.key === "Enter") 
        {
            request_submit();
        }
        else if (event.key === "z")
        {
            request_undo();
        }
        else if (event.key === "y")
        {
            request_redo();
        }
        else if (event.key === "h")
        {
            request_hint();
        }
        else if (event.key === ",")
        {
            request_prev();
        }
        else if (event.key === ".")
        {
            request_next();
        }
    });
    
    go_to_center();
}

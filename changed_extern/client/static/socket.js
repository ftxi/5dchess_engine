'use strict';

const socket = io();

// Function to request data from the server
function request_data() {
    socket.emit('request_data', { request: 'Send me user data!' });
}

function report_click(l, t, c, x, y) {
    socket.emit('click', {l: l, t: t, c: c, x: x, y: y});
}

function report_right_click(l, t, c, x, y) {
    socket.emit('right_click', {l: l, t: t, c: c, x: x, y: y});
}

function request_prev() {
    if(request_prev.enabled)
    {
        socket.emit('request_prev');
    }
}
function request_next() {
    if(request_next.enabled)
    {
        let value = document.getElementById("next-select").value;
        socket.emit('request_next', value);
    }
}

function request_undo() {
    if(request_undo.enabled)
    {
        socket.emit('request_undo');
    }
}

function request_redo() {
    if(request_redo.enabled)
    {
        socket.emit('request_redo');
    }
}

function request_submit() {
    if(request_submit.enabled)
    {
        socket.emit('request_submit');
    }
}

function request_hint() {
    if(request_hint.enabled)
    {
        socket.emit('request_hint');
    }
}

function request_load() {
    let pgn = document.getElementById('txt-area').value;
    socket.emit('request_load', pgn);
}

function request_pgn() {
    socket.emit('request_pgn');
}

function request_perft(depth, timeout, dynamic, use_tt, parallel) {
    const resultSpan = document.getElementById('perft-result');
    resultSpan.innerText = 'Calculating...';
    resultSpan.style.color = '#888';
    socket.emit('request_perft', { 
        depth: depth, 
        timeout: timeout > 0 ? timeout : null,
        dynamic: dynamic, 
        use_tt: use_tt, 
        parallel: parallel 
    });
}

function request_count_actions() {
    const resultSpan = document.getElementById('perft-result');
    resultSpan.innerText = 'Calculating...';
    resultSpan.style.color = '#888';
    socket.emit('request_count_actions');
}

socket.on('response_load', function(data) {
    alert(data);
});

socket.on('response_perft', function(data) {
    const resultSpan = document.getElementById('perft-result');
    if (data.error) {
        resultSpan.innerText = 'Error: ' + data.error;
        resultSpan.style.color = '#f44';
    } else {
        let modeText = ` (${data.mode || 'single'})`;
        let completedText = data.completed === false ? ' [TIMEOUT]' : '';
        resultSpan.innerText = `Count: ${data.count.toLocaleString()} | Time: ${data.time}s | Rate: ${data.rate.toLocaleString()} nodes/s${modeText}${completedText}`;
        resultSpan.style.color = data.completed === false ? '#ff8' : '#4f4';
    }
});

socket.on('response_text', function(data) {
    document.getElementById('text-window').innerHTML = data;
});

socket.on('response_pgn', function(data){
    console.log(data);
    document.getElementById('export-area').value = data;
});

// Listen for the response from the server
socket.on('response_data', function(data) {
    console.log('Data received from server:', data);

    function change_btn_status(btn, status, callback)
    {
        if(status)
        {
            btn.style.display = 'block';
            if(status == "enabled")
            {
                btn.classList.remove('btn-inactive');
                btn.classList.add('btn-active');
                callback.enabled = true;
            }
            else if(status == "disabled")
            {
                btn.classList.remove('btn-active');
                btn.classList.add('btn-inactive');
                callback.enabled = false;
            }
        }
        else
        {
            btn.style.display = 'none';
            callback.enabled = true;
        }
    }

    change_btn_status(document.getElementById('submit-btn'), data['submit-button'], request_submit);
    change_btn_status(document.getElementById('undo-btn'), data['undo-button'], request_undo);
    change_btn_status(document.getElementById('redo-btn'), data['redo-button'], request_redo);
    change_btn_status(document.getElementById('prev-btn'), data['prev-button'], request_prev);
    change_btn_status(document.getElementById('next-btn'), data['next-button'], request_next);
    change_btn_status(document.getElementById('hint-btn'), data['hint-button'], request_hint);
    let next_options = data['next-options'];
    if(next_options && Object.keys(next_options).length > 0)
    {
        const sel = document.getElementById("next-select");
        let prev_value = sel.value;
        sel.classList.remove('select-inactive');
        sel.classList.add('select-active');
        sel.innerHTML = "";
        for (const [key, value] of Object.entries(next_options)) 
        {
            const opt = document.createElement("option");
            opt.value = key;
            opt.textContent = value;
            console.log({key, value});
            sel.appendChild(opt);
        }
        if ([...sel.options].some(opt => opt.value === prev_value)) 
        {
            sel.value = prev_value;
        }
    }
    else
    {
        const sel = document.getElementById("next-select");
        sel.classList.remove('select-active');
        sel.classList.add('select-inactive');
    }

    const ms = document.getElementById('match-status');
    if(data['match-status'])
    {
        ms.style.display = 'block';
        ms.innerText = data['match-status'];
    }
    else
    {
        ms.style.display = 'none';
    }

    window.chessBoardCanvas.setData(data);
});

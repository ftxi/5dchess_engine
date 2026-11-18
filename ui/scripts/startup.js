// Startup state is shared; importing this module does not start the worker.
const loadedItems = new Set();
let finished = false;
const screen = document.getElementById('loadingScreen');
const status = document.getElementById('loadingStatus');
const blockedElements = [];

function updateLoading() {
    if (finished) return;
    let step, message;
    if (!loadedItems.has('engine') && !loadedItems.has('pieces')) {
        step = 2;
        message = 'Loading WebAssembly and chess pieces';
    } else if (!loadedItems.has('engine')) {
        step = 3;
        message = 'Loading WebAssembly';
    } else if (!loadedItems.has('pieces')) {
        step = 3;
        message = 'Loading chess pieces';
    } else if (!loadedItems.has('interface')) {
        step = 4;
        message = 'Preparing the interface';
    } else if (!loadedItems.has('position') || !loadedItems.has('boards')) {
        step = 5;
        message = 'Loading the starting position';
    } else {
        finished = true;
        for (const element of blockedElements) element.inert = false;
        if (screen) screen.remove();
        return;
    }
    if (status) status.textContent = `[${step}/5] ${message}…`;
}

export function reportLoaded(item) {
    loadedItems.add(item);
    updateLoading();
}

export let worker;
const pendingMessages = [];

export function startWorker() {
    if (worker) return worker;
    if (screen?.parentElement) {
        for (const element of screen.parentElement.children) {
            if (element !== screen && !element.inert) {
                element.inert = true;
                blockedElements.push(element);
            }
        }
    }
    updateLoading();
    worker = new Worker(window.worker_path, { type: 'module' });
    worker.onmessage = event => {
        if (event.data.type === 'ready') reportLoaded('engine');
        pendingMessages.push(event);
    };
    return worker;
}

export function connectWorker(onmessage) {
    worker.onmessage = event => {
        if (event.data.type === 'ready') reportLoaded('engine');
        onmessage(event);
    };
    for (const event of pendingMessages) onmessage(event);
    pendingMessages.length = 0;
}

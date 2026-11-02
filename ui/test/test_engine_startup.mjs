import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const source = (await readFile(new URL('../scripts/startup.js', import.meta.url), 'utf8'))
    .replace(/^export /gm, '');

function loadingDocument() {
    const status = { textContent: '[1/5] Preparing the analyzer…' };
    const screen = { removed: false, remove() { this.removed = true; } };
    return { status, screen, getElementById: id => id === 'loadingStatus' ? status : screen };
}

for (const early of [true, false]) {
    test(`engine messages arrive in order when the engine is ready ${early ? 'before' : 'after'} the UI`, () => {
        let worker;
        const received = [];
        const context = vm.createContext({
            document: loadingDocument(),
            window: { worker_path: 'scripts/worker.js' },
            Worker: class {
                constructor(path, options) {
                    assert.equal(path, 'scripts/worker.js');
                    assert.equal(options.type, 'module');
                    worker = this;
                }
            },
            onmessage: event => received.push(event.data)
        });
        vm.runInContext(source, context);
        assert.equal(worker, undefined, 'importing startup must not create a worker');
        vm.runInContext('startWorker()', context);
        assert.equal(vm.runInContext('startWorker()', context), worker);
        const send = type => worker.onmessage({ data: { type } });
        if (early) {
            send('engine_version');
            send('ready');
            assert.deepEqual(received, []);
        }
        vm.runInContext('connectWorker(onmessage)', context);
        if (!early) {
            send('engine_version');
            send('ready');
        }
        send('data');
        assert.deepEqual(received.map(message => message.type), ['engine_version', 'ready', 'data']);
        // Connecting again must not replay the startup messages twice.
        vm.runInContext('connectWorker(onmessage)', context);
        assert.equal(received.length, 3);
    });
}

for (const first of ['engine', 'pieces', 'both']) {
    test(`loading checkpoints follow ${first} completing first and stop after the board is displayed`, () => {
        const document = loadingDocument();
        const context = vm.createContext({ document, window: {}, Worker: class {} });
        assert.match(document.status.textContent, /^\[1\/5\]/);
        vm.runInContext(source, context);
        assert.match(document.status.textContent, /^\[1\/5\]/);
        vm.runInContext('startWorker()', context);
        assert.equal(document.status.textContent, '[2/5] Loading WebAssembly and chess pieces…');
        if (first === 'engine') {
            vm.runInContext("reportLoaded('engine')", context);
            assert.equal(document.status.textContent, '[3/5] Loading chess pieces…');
            vm.runInContext("reportLoaded('pieces')", context);
        } else if (first === 'pieces') {
            vm.runInContext("reportLoaded('pieces')", context);
            assert.equal(document.status.textContent, '[3/5] Loading WebAssembly…');
            vm.runInContext("reportLoaded('engine')", context);
        } else {
            vm.runInContext("reportLoaded('engine'); reportLoaded('pieces')", context);
        }
        assert.equal(document.status.textContent, '[4/5] Preparing the interface…');
        vm.runInContext("reportLoaded('interface')", context);
        assert.equal(document.status.textContent, '[5/5] Loading the starting position…');
        assert.equal(document.screen.removed, false);
        vm.runInContext("reportLoaded('position'); reportLoaded('engine')", context);
        assert.equal(document.screen.removed, false, 'Board preparation must also finish');
        vm.runInContext("reportLoaded('boards')", context);
        assert.equal(document.screen.removed, true);
        assert.equal(document.status.textContent, '[5/5] Loading the starting position…');
    });
}

test('early interface and repeated reports still wait for every startup dependency', () => {
    const document = loadingDocument();
    const sibling = { inert: false };
    const alreadyBlocked = { inert: true };
    document.screen.parentElement = { children: [document.screen, sibling, alreadyBlocked] };
    const context = vm.createContext({ document, window: {}, Worker: class {} });
    vm.runInContext(source, context);
    assert.equal(sibling.inert, false);
    vm.runInContext('startWorker()', context);
    assert.equal(sibling.inert, true);
    vm.runInContext("reportLoaded('interface'); reportLoaded('pieces'); reportLoaded('pieces')", context);
    assert.equal(document.status.textContent, '[3/5] Loading WebAssembly…');
    assert.equal(document.screen.removed, false);
    vm.runInContext("reportLoaded('engine')", context);
    assert.equal(document.status.textContent, '[5/5] Loading the starting position…');
    vm.runInContext("reportLoaded('boards'); reportLoaded('boards')", context);
    assert.equal(document.screen.removed, false, 'Prepared boards still need a rendered position');
    vm.runInContext("reportLoaded('position'); reportLoaded('position')", context);
    assert.equal(document.screen.removed, true);
    assert.equal(sibling.inert, false);
    assert.equal(alreadyBlocked.inert, true);
});

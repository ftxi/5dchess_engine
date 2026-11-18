import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
const { createImportDiagnostics, formatDiagnosticLocation } = await import('data:text/javascript,' + encodeURIComponent(
    await readFile(new URL('../scripts/import-diagnostics.js', import.meta.url), 'utf8')));

class Element {
    constructor() { this.events = {}; this.attrs = {}; this.style = {}; this.children = []; this.hidden = false; }
    addEventListener(name, callback) { this.events[name] = callback; }
    setAttribute(key, value) { this.attrs[key] = value; }
    removeAttribute(key) { delete this.attrs[key]; }
    replaceChildren(...children) { this.children = children; }
    matches() { return true; }
    getClientRects() { return []; }
    getBoundingClientRect() { return { top: 0, bottom: 120 }; }
    fire(name) { this.events[name]?.(); }
}
globalThis.ResizeObserver = class { constructor(callback) { this.callback = callback; } observe() { this.callback(); } };
function fixture(text = 'line one\nline two') {
    const elements = Object.fromEntries(['textarea', 'overlay', 'mirror', 'row', 'location', 'message', 'heading'].map(key => [key, new Element()]));
    Object.assign(elements.textarea, { value: text, clientWidth: 241, clientHeight: 120, scrollLeft: 0, scrollTop: 0,
        ownerDocument: { createElement: () => new Element(), createTextNode: textContent => ({ textContent }) } });
    const canceled = [];
    const controller = createImportDiagnostics({ ...elements, cancel: id => canceled.push(id) });
    function fail(diagnostic = { message: 'Bad PGN', range: { start: 9, end: 13 } }) {
        const request = controller.begin();
        controller.receive({ requestId: request.requestId, success: false, diagnostic });
    }
    return { ...elements, controller, canceled, fail };
}

test('location labels use editor offsets, multiline ranges and missing-input positions', () => {
    assert.equal(formatDiagnosticLocation('a😀\nb', { start: 4, end: 5 }), 'line 2, 1–1');
    assert.match(formatDiagnosticLocation('a\nb', { start: 0, end: 3 }), /line 1, 1 – line 2, 1/);
    assert.equal(formatDiagnosticLocation('a\n', { start: 2, end: 2 }), 'line 2, 1 (missing input)');
});

test('hover previews, clicking pins, leaving preserves the pin, clicking again unpins', () => {
    const f = fixture();
    f.fail();
    assert.equal(f.overlay.hidden, true);
    f.location.fire('mouseenter');
    assert.equal(f.overlay.hidden, false);
    assert.equal(f.mirror.children[1].textContent, 'line');
    f.location.fire('click');
    f.location.fire('mouseleave');
    assert.equal(f.overlay.hidden, false);
    assert.equal(f.location.attrs['aria-pressed'], 'true');
    f.location.fire('click');
    assert.equal(f.overlay.hidden, true);
});

test('editing removes pinned highlighting but retains identical diagnostic wording', () => {
    const f = fixture();
    f.fail({ message: 'Royal exposed.', range: { start: 0, end: 4 }, canAllowIllegalHistory: true });
    const previousHeading = f.heading.textContent;
    const previousMessage = f.message.textContent;
    f.location.fire('click');
    f.textarea.value += ' fixed';
    f.textarea.fire('input');
    assert.equal(f.overlay.hidden, true);
    assert.equal(f.row.attrs['data-state'], 'edited');
    assert.equal(f.heading.textContent, previousHeading);
    assert.equal(f.message.textContent, previousMessage);
    assert.equal(f.textarea.attrs['aria-invalid'], undefined);
    assert.equal(f.location.disabled, true);
    assert.match(f.message.textContent, /Allow submit with checks/);
    f.location.fire('mouseenter');
    assert.equal(f.overlay.hidden, true);
});

test('stale load responses are ignored; only accepted success can finish an import', () => {
    const f = fixture();
    const first = f.controller.begin();
    const second = f.controller.begin();
    assert.deepEqual(f.canceled, [first.requestId]);
    assert.equal(f.controller.receive({ requestId: first.requestId, success: true }), false);
    assert.equal(f.controller.receive({ requestId: second.requestId, success: true }), true);
    assert.equal(f.textarea.readOnly, true);
    assert.equal(f.controller.complete(first.requestId), false);
    assert.equal(f.controller.complete(second.requestId), true);
    assert.equal(f.row.attrs['data-state'], 'empty');
    assert.equal(f.heading.textContent, '');
    assert.equal(f.message.textContent, '');
    assert.equal(f.textarea.readOnly, false);
    const third = f.controller.begin();
    f.textarea.value += '!';
    f.textarea.fire('input');
    assert.equal(f.controller.receive({ requestId: third.requestId, success: true }), false);
});

test('range-free runtime errors remain readable; EOF markers and scrolling use the native viewport', () => {
    const f = fixture('<unsafe>\n');
    f.fail({ message: '<plain text error>', range: null });
    assert.equal(f.location.hidden, false);
    assert.equal(f.location.disabled, true);
    assert.equal(f.heading.textContent, 'Error:');
    assert.equal(f.message.textContent, '<plain text error>');
    f.fail({ message: 'Missing input', range: { start: 9, end: 9 } });
    f.location.fire('focus');
    assert.equal(f.mirror.children[1].className, 'pgn-error-caret');
    f.textarea.scrollTop = 120;
    f.textarea.scrollLeft = 8;
    f.textarea.fire('scroll');
    assert.equal(f.overlay.style.width, '241px');
    assert.equal(f.mirror.style.transform, 'translate(-8px, -120px)');
});


test('closing an import cancels its candidate and prevents a late success from being accepted', () => {
    const f = fixture();
    const request = f.controller.begin();
    f.controller.cancelPending();
    assert.deepEqual(f.canceled, [request.requestId]);
    assert.equal(f.controller.receive({ requestId: request.requestId, success: true }), false);
    assert.equal(f.textarea.readOnly, false);
});


test('new checks suspend old diagnostics and restore current errors after a failed retry', () => {
    const f = fixture();
    assert.equal(f.row.attrs['data-state'], 'empty');
    assert.equal(f.location.hidden, true);
    f.fail();
    assert.equal(f.heading.textContent, 'Error at line 2, 1–4');
    assert.equal(f.textarea.attrs['aria-invalid'], 'true');
    f.location.fire('click');
    const request = f.controller.begin();
    assert.equal(f.row.attrs['data-state'], 'loading');
    assert.equal(f.heading.textContent, 'Checking import…');
    assert.equal(f.textarea.attrs['aria-busy'], 'true');
    assert.equal(f.textarea.attrs['aria-invalid'], undefined);
    assert.equal(f.location.disabled, true);
    assert.equal(f.overlay.hidden, true);
    f.location.fire('focus');
    f.location.fire('click');
    assert.equal(f.overlay.hidden, true);
    f.controller.receive({ requestId: request.requestId, success: false,
        diagnostic: { message: 'Still invalid', range: { start: 0, end: 13 } } });
    assert.equal(f.row.attrs['data-state'], 'error');
    assert.equal(f.textarea.attrs['aria-busy'], undefined);
    assert.equal(f.textarea.attrs['aria-invalid'], 'true');
    assert.equal(f.location.disabled, false);
    assert.equal(f.heading.textContent, 'Error at line 1, 1 – line 2, 4');
    assert.equal(f.overlay.hidden, true);
});

test('edits cancel checks and late failures cannot replace the retained diagnostic', () => {
    const f = fixture();
    f.fail();
    const request = f.controller.begin();
    f.textarea.value += '!';
    f.textarea.fire('input');
    assert.deepEqual(f.canceled, [request.requestId]);
    assert.equal(f.controller.receive({ requestId: request.requestId, success: false,
        diagnostic: { message: 'Outdated failure' } }), false);
    assert.equal(f.message.textContent, 'Bad PGN');
    assert.equal(f.row.attrs['data-state'], 'edited');
    assert.equal(f.location.disabled, true);
    f.controller.begin();
    f.controller.cancelPending();
    assert.equal(f.row.attrs['data-state'], 'edited');
});

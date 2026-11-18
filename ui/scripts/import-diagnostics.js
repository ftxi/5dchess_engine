// Offsets in diagnostics are UTF-16 positions in the submitted textarea value.
export function formatDiagnosticLocation(text, range) {
    const position = offset => {
        const prefix = text.slice(0, offset);
        const line = prefix.split('\n').length;
        return { line, column: offset - prefix.lastIndexOf('\n') };
    };
    const start = position(range.start), end = position(Math.max(range.start, range.end - 1));
    if (range.start === range.end) return `line ${start.line}, ${start.column} (missing input)`;
    if (start.line === end.line) return `line ${start.line}, ${start.column}–${end.column}`;
    return `line ${start.line}, ${start.column} – line ${end.line}, ${end.column}`;
}

export function createImportDiagnostics({ textarea, overlay, mirror, row, location, message, heading, cancel }) {
    let nextId = 0, active = null, diagnostic = null, snapshot = '', range = null;
    let pinned = false, hovered = false, focused = false, edited = false;
    const doc = textarea.ownerDocument;

    function sync() {
        // clientWidth excludes the native scrollbar and borders, just as the
        // overlay viewport does. Both text surfaces share their typography CSS.
        overlay.style.width = `${textarea.clientWidth}px`;
        overlay.style.height = `${textarea.clientHeight}px`;
        mirror.style.transform = `translate(${-textarea.scrollLeft}px, ${-textarea.scrollTop}px)`;
    }
    function renderHighlight(reveal = false) {
        const visible = range && !edited && active === null && (pinned || hovered || focused);
        overlay.hidden = !visible;
        location.setAttribute('aria-pressed', String(pinned));
        if (!visible) return;
        const before = doc.createTextNode(snapshot.slice(0, range.start));
        const highlight = doc.createElement(range.start === range.end ? 'span' : 'mark');
        highlight.className = range.start === range.end ? 'pgn-error-caret' : 'pgn-error-mark';
        highlight.textContent = snapshot.slice(range.start, range.end);
        mirror.replaceChildren(before, highlight, doc.createTextNode(snapshot.slice(range.end) + '\u200b'));
        sync();
        if (reveal) {
            const target = highlight.getClientRects()[0];
            const viewport = overlay.getBoundingClientRect();
            if (target && (target.top < viewport.top || target.bottom > viewport.bottom)) {
                textarea.scrollTop = Math.max(0, textarea.scrollTop + target.top - viewport.top - textarea.clientHeight / 3);
                sync();
            }
        }
    }
    function updateState() {
        const state = active ? 'loading' : diagnostic ? (edited ? 'edited' : 'error') : 'empty';
        row.setAttribute('data-state', state);
        const progress = active?.preparing
            ? (active.checking ? 'Checking import and preparing boards…' : 'Preparing boards…')
            : active?.checking ? 'Checking import…' : 'Loading position…';
        heading.textContent = active ? progress
            : diagnostic ? (range ? `Error at ${formatDiagnosticLocation(snapshot, range)}` : 'Error:') : '';
        row.setAttribute('data-has-error', String(Boolean(diagnostic)));
        row.setAttribute('data-has-range', String(Boolean(range)));
        location.hidden = !diagnostic && active === null;
        location.disabled = !range || edited || active !== null;
        if (state === 'error') textarea.setAttribute('aria-invalid', 'true');
        else textarea.removeAttribute('aria-invalid');
        renderHighlight();
    }
    function cancelPending() {
        if (active !== null) cancel?.(active.id);
        active = null;
        textarea.readOnly = false;
        textarea.removeAttribute('aria-busy');
        updateState();
    }
    function edit() {
        cancelPending();
        pinned = hovered = focused = false;
        edited = true;
        updateState();
    }
    textarea.addEventListener('input', edit);
    textarea.addEventListener('scroll', sync);
    location.addEventListener('mouseenter', () => { hovered = true; renderHighlight(true); });
    location.addEventListener('mouseleave', () => { hovered = false; renderHighlight(); });
    location.addEventListener('focus', () => { focused = location.matches(':focus-visible'); renderHighlight(true); });
    location.addEventListener('blur', () => { focused = false; renderHighlight(); });
    location.addEventListener('click', () => {
        if (edited || active !== null || !range) return;
        pinned = !pinned;
        renderHighlight(true);
    });
    const observer = new ResizeObserver(sync);
    observer.observe(textarea);
    updateState();

    return {
        edit,
        cancelPending,
        begin() {
            if (active !== null) cancel?.(active.id);
            active = { id: ++nextId, text: textarea.value, checking: true, preparing: false };
            pinned = hovered = focused = false;
            textarea.readOnly = false;
            textarea.setAttribute('aria-busy', 'true');
            updateState();
            return { requestId: active.id, pgn: active.text };
        },
        setPreparing(requestId, preparing) {
            if (!active || requestId !== active.id) return;
            active.preparing = preparing;
            textarea.readOnly = !active.checking && !active.preparing;
            updateState();
        },
        receive(result) {
            if (!active || result.requestId !== active.id || textarea.value !== active.text) return false;
            if (result.success) {
                active.checking = false;
                // Edits may still cancel preparation. Freeze them only once
                // both tasks finish and the commit/ack interval begins.
                textarea.readOnly = !active.preparing;
                updateState();
                return true;
            }
            textarea.removeAttribute('aria-busy');
            snapshot = active.text;
            active = null;
            diagnostic = result.diagnostic || { message: result.message || 'Could not load PGN.' };
            const candidate = diagnostic.range;
            range = candidate && Number.isInteger(candidate.start) && Number.isInteger(candidate.end)
                && candidate.start >= 0 && candidate.start <= candidate.end && candidate.end <= snapshot.length
                ? candidate : null;
            edited = pinned = hovered = focused = false;
            message.textContent = diagnostic.message + (diagnostic.canAllowIllegalHistory
                ? ' To inspect this game, enable “Allow submit with checks” in Settings.' : '');
            updateState();
            return false;
        },
        complete(requestId) {
            if (!active || active.id !== requestId) return false;
            active = null;
            diagnostic = range = null;
            pinned = hovered = focused = edited = false;
            textarea.readOnly = false;
            textarea.removeAttribute('aria-busy');
            textarea.removeAttribute('aria-invalid');
            message.textContent = '';
            updateState();
            return true;
        },
    };
}

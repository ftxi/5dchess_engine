// color.js
// Utilities for loading theme colors, resolving CSS variable colors, and applying saturation fade

function _hslToString(h, s, l, a = 1) {
    const sPct = Math.round(s * 100);
    const lPct = Math.round(l * 100);
    if (typeof a === 'number' && a < 1) {
        return `hsla(${Math.round(h)}, ${sPct}%, ${lPct}%, ${a})`;
    } else {
        return `hsl(${Math.round(h)}, ${sPct}%, ${lPct}%)`;
    }
}

function _parseColorToHsla(color) {
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = 1;
    const ctx = canvas.getContext('2d');
    
    // Let the browser handle parsing the input string
    ctx.fillStyle = color;
    ctx.fillRect(0, 0, 1, 1);
    
    const [r, g, b, a] = ctx.getImageData(0, 0, 1, 1).data;
    const alpha = parseFloat((a / 255).toFixed(2));

    const rNorm = r / 255, gNorm = g / 255, bNorm = b / 255;
    const max = Math.max(rNorm, gNorm, bNorm);
    const min = Math.min(rNorm, gNorm, bNorm);
    const delta = max - min;

    let h = 0, s = 0, l = (max + min) / 2;

    if (delta !== 0) {
        s = l > 0.5 ? delta / (2 - max - min) : delta / (max + min);
        
        switch (max) {
            case rNorm: h = (gNorm - bNorm) / delta + (gNorm < bNorm ? 6 : 0); break;
            case gNorm: h = (bNorm - rNorm) / delta + 2; break;
            case bNorm: h = (rNorm - gNorm) / delta + 4; break;
        }
        h /= 6;
    }

    return {
        h: Math.round(h * 360),
        s: s,
        l: l,
        a: alpha
    };
}

function applyFadeToColorString(colorStr, fade) {
    const hsla = _parseColorToHsla(colorStr);
    if (!hsla) return null;
    const factor = 1 - Math.max(0, Math.min(1, fade));
    const newS = hsla.s * factor;
    return _hslToString(hsla.h, newS, hsla.l, hsla.a);
}

function loadColors() {
    const s = getComputedStyle(document.documentElement);
    const colors = {
        spGridWhite: s.getPropertyValue('--sp-grid-white').trim() || '#ffffff',
        spGridBlack: s.getPropertyValue('--sp-grid-black').trim() || '#f5f5f5',
        present: s.getPropertyValue('--present').trim() || 'rgba(219,172,52,0.4)',
        boardMarginBlack: s.getPropertyValue('--board-margin-black').trim() || '#555555',
        boardMarginWhite: s.getPropertyValue('--board-margin-white').trim() || '#dfdfdf',
        squareBlack: s.getPropertyValue('--square-black').trim() || '#7f7f7f',
        squareWhite: s.getPropertyValue('--square-white').trim() || '#cccccc',
        squareFuzzy: s.getPropertyValue('--square-fuzzy').trim() || '#a6a6a6',
        arrowWhiteTop: s.getPropertyValue('--arrow-white-top').trim() || 'rgba(255,255,255,0.8)',
        debugRed: s.getPropertyValue('--debug-red').trim() || 'red'
    };
    return colors;
}

function applyFadeToColors(colors, fade) {
    const out = Object.assign({}, colors);
    for (const k of Object.keys(colors)) {
        const val = colors[k];
        const parsed = _parseColorToHsla(val);
        if (parsed) {
            out[k] = applyFadeToColorString(val, fade);
        } else {
            out[k] = val;
        }
    }
    return out;
}

function resolveColor(token, activeFade) {
    if (!token) return null;
    const t = String(token).trim();
    let varName = null;
    if (t.startsWith('--')) varName = t;
    else {
        const m = t.match(/^var\((--[a-zA-Z0-9_-]+)\)$/);
        if (m) varName = m[1];
    }
    if (varName) {
        const val = getComputedStyle(document.documentElement).getPropertyValue(varName).trim();
        if (!val) return val;
        if (varName === '--highlight-check' || varName === '--highlight-phantom-board') return val; // check stays unchanged
        if (typeof activeFade === 'number' && activeFade > 0) {
            return applyFadeToColorString(val, activeFade) || val;
        }
        return val;
    }
    if (t.startsWith('#') || t.startsWith('rgb') || t.startsWith('hsl') || t.indexOf('(') !== -1) {
        if (typeof activeFade === 'number' && activeFade > 0) {
            return applyFadeToColorString(t, activeFade) || t;
        }
        return t;
    }
    return t;
}

export { loadColors, applyFadeToColors, applyFadeToColorString, resolveColor};

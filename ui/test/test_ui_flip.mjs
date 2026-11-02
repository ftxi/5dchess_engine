import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

// Rendering dependencies are browser-only; exercise the actual board methods
// with a recording context and a single upright piece image.
const source = (await readFile(new URL('../scripts/draw.js', import.meta.url), 'utf8'))
    .replace(/^import .*;\n/gm, '');
const { default: ChessBoardCanvas, safeOpacity } = await import(`data:text/javascript;base64,${Buffer.from(
    `const chooseLOD = () => ({ P: "piece" });
    // Geometry-only composition stub; actual grouped pixels are checked separately.
    function composeSurface(ctx, width, height, scale, x, y, opacity, draw) {
        const surface = {
            save: () => ctx.save(), restore: () => ctx.restore(),
            get globalAlpha() { return ctx.globalAlpha; },
            set globalAlpha(value) { ctx.globalAlpha = value; },
            get fillStyle() { return ctx.fillStyle; },
            set fillStyle(value) { ctx.fillStyle = value; },
            fillRect: (a, b, w, h) => ctx.fillRect(a + x, b + y, w, h),
            fillText: (text, a, b) => ctx.fillText(text, a + x, b + y),
            measureText: text => ctx.measureText(text),
        };
        ctx.save(); ctx.globalAlpha *= opacity; draw(surface); ctx.restore();
    }
    ` + source
).toString('base64')}`);

function board(width, height, flipped, preserveFiles) {
    return Object.assign(Object.create(ChessBoardCanvas.prototype), {
        boardLengthX: width, boardLengthY: height, squareSize: 10,
        boardSkipX: width * 10 + 20, boardSkipY: height * 10 + 20,
        backgroundShiftX: 10, backgroundShiftY: 10, fontSizeforLT: 16,
        flipped, doNotReverseFilesWhenFlipped: preserveFiles,
        infCanvas: {
            cameraCurrent: {
                getZoomLevel: () => 5, x: 0, y: 0,
                screenToWorld: () => ({ x: 0, y: 0 }),
            }, pixelRatio: 1,
            viewportSize: { width: 800, height: 600 },
        },
    });
}

test('local selection highlights merge with worker highlights and invalidate the viewport cache', () => {
    const canvas = board(8, 8, false, false);
    const original = { l: 0, t: 0, c: false, x: 1, y: 1 };
    const preview = { ...original, x: 2 };
    canvas.data = { boards: [], highlights: [{ color: 'purple', coordinates: [original] }] };
    canvas.filterCache = { bounds: [] };
    canvas._resolveColor = value => value;
    let redraws = 0;
    canvas.infCanvas.startAnimation = () => redraws++;
    canvas.setSelectionHighlights([{ color: 'purple', coordinates: [preview] }]);
    canvas._filterBoardData(0, 0, 0, 0);
    assert.deepEqual(canvas.filterCache.filteredCoordinateHighlight.purple, [original, preview]);
    canvas.setSelectionHighlights([]);
    canvas._filterBoardData(0, 0, 0, 0);
    assert.deepEqual(canvas.filterCache.filteredCoordinateHighlight.purple, [original]);
    assert.equal(redraws, 2);
    assert.deepEqual(canvas.data.highlights[0].coordinates, [original]);
});

test('square coordinates and hit detection agree in both flip modes and board sizes', () => {
    for (const [width, height] of [[8, 8], [7, 5], [1, 1]]) {
        for (const flipped of [false, true]) {
            for (const preserveFiles of [false, true]) {
                const canvas = board(width, height, flipped, preserveFiles);
                for (const l of [-2, 0, 3]) {
                    for (const t of [-1, 2]) {
                        for (const c of [false, true]) {
                            for (let x = 0; x < width; x++) {
                                for (let y = 0; y < height; y++) {
                                    const pos = { l, t, c, x, y };
                                    const [px, py] = canvas._getCoordinate(pos);
                                    assert.equal(px, (2 * t + Number(c)) * canvas.boardSkipX
                                        + (flipped && !preserveFiles ? width - 1 - x : x) * 10);
                                    const hit = canvas.worldToBoard(px + 5, py + 5);
                                    assert.deepEqual({ ...hit, l: hit.l || 0 }, pos);
                                }
                            }
                        }
                    }
                }
                // The horizontal gap must remain outside the board after reflection.
                const gap = canvas.worldToBoard(width * 10 + 5, 5);
                assert(gap.x < 0 || gap.x >= width);
            }
        }
    }
});

test('rendering keeps pieces, checkerboard, labels, highlights and arrows aligned', () => {
    for (const width of [7, 8]) {
        for (const flipped of [false, true]) {
            for (const preserveFiles of [false, true]) {
                const canvas = board(width, 8, flipped, preserveFiles);
                const parsed = Array.from({ length: 8 }, () => Array(width).fill(0));
                parsed[0][0] = 'P';
                const pos = { l: 2, t: 1, c: false, x: 0, y: 7 };
                canvas.data = {};
                canvas.colors = { squareWhite: 'white', squareBlack: 'black' };
                canvas.fontSizeforXY = 3;
                canvas._filterBoardData = () => {};
                canvas.filterCache = {
                    filteredBoards: [{ ...pos, parsed }],
                    filteredCoordinateHighlight: { highlight: [pos] },
                    filteredArrowHighlight: { arrow: [{ from: pos, to: { ...pos, x: width - 1 } }] },
                    filteredBoardHighlight: {}, filteredTimelineHighlight: {},
                };
                canvas.cameraElement = {};
                const rectangles = [], images = [], labels = [], arrows = [];
                canvas._drawArrow = (...args) => arrows.push(args.slice(1));
                const ctx = {
                    save() {}, restore() {}, fill() {},
                    fillRect(...args) { rectangles.push({ color: this.fillStyle, args }); },
                    drawImage(...args) { images.push(args); },
                    fillText(text, x, y) { labels.push({ text, x, y, color: this.fillStyle }); },
                    measureText() { return { actualBoundingBoxLeft: 0, actualBoundingBoxAscent: 2, actualBoundingBoxDescent: 0 }; },
                    createLinearGradient() { return { addColorStop() {} }; },
                };
                canvas.drawBoards(ctx, -3, 3, 0, 4);
                const [px, py] = canvas._getCoordinate(pos);
                assert.deepEqual(images, [['piece', px, py, 10, 10]]);
                assert(rectangles.some(rect => rect.color === 'white' && rect.args[0] === px && rect.args[1] === py));
                assert(rectangles.some(rect => rect.color === 'highlight' && rect.args[0] === px && rect.args[1] === py));
                const [tx, ty] = canvas._getCoordinate({ ...pos, x: width - 1 });
                assert.deepEqual(arrows, [[px + 5, py + 5, tx + 5, ty + 5]]);
                const files = labels.filter(label => /^[a-h]$/.test(label.text)).sort((a, b) => a.x - b.x);
                const expected = Array.from({ length: width }, (_, i) => String.fromCharCode(97 + i));
                assert.deepEqual(files.map(label => label.text), flipped && !preserveFiles ? expected.reverse() : expected);
                const rank8 = labels.find(label => label.text === '8');
                assert.equal(rank8.y, py + 2.3);
                assert.equal(rank8.color, canvas._getLabelTextColor(0, canvas._displayBoardCol(0)));
            }
        }
    }
});

test('changing the preference redraws without moving boards or the camera', () => {
    const canvas = board(8, 8, true, false);
    let redraws = 0;
    canvas.infCanvas.startAnimation = () => redraws++;
    canvas.infCanvas.cameraTarget = { x: -240, y: 160 };
    const camera = { ...canvas.infCanvas.cameraTarget };
    const before = canvas._getCoordinate({ l: 2, t: 1, c: false, x: 0, y: 0 });
    canvas.setDoNotReverseFilesWhenFlipped(true);
    assert.equal(redraws, 1);
    assert.deepEqual(canvas.infCanvas.cameraTarget, camera);
    assert.equal(canvas._displayL(2), -2);
    const after = canvas._getCoordinate({ l: 2, t: 1, c: false, x: 0, y: 0 });
    assert.equal(before[0] - after[0], 70);
    assert.equal(before[1], after[1]);
});

test('walls merge with margins at every zoom and ignore clicks, previews, labels and highlights', () => {
    for (const flipped of [false, true]) {
        for (const preserveFiles of [false, true]) {
            for (const c of [false, true]) {
                for (const phantom of [false, true]) {
                    for (const zoom of [-5, 5]) {
                        const canvas = board(7, 5, flipped, preserveFiles);
                        const parsed = Array.from({ length: 5 }, () => Array(7).fill('1'));
                        parsed[2][2] = parsed[2][3] = parsed[4][0] = '#';
                        const wall = { l: 1, t: 2, c, x: 2, y: 2 };
                        const data = { ...wall, fen: 'display', parsed, isPhantom: phantom };
                        canvas.data = { boards: [data] };
                        canvas.colors = { boardMarginWhite: 'white-margin', boardMarginBlack: 'black-margin',
                            squareWhite: 'white', squareBlack: 'black', squareFuzzy: 'fuzzy', phantomBoardOpacity: 0.3 };
                        canvas.boardMargin = 4;
                        canvas.fontSizeforXY = 3;
                        canvas.cameraElement = {};
                        canvas.infCanvas.cameraCurrent.getZoomLevel = () => zoom;
                        canvas._filterBoardData = () => {};
                        canvas.filterCache = { filteredBoards: [data],
                            filteredCoordinateHighlight: { highlight: [wall] },
                            filteredArrowHighlight: {}, filteredBoardHighlight: {}, filteredTimelineHighlight: {} };
                        const rectangles = [], labels = [], stack = [];
                        const ctx = {
                            globalAlpha: 1,
                            save() { stack.push({ fillStyle: this.fillStyle, globalAlpha: this.globalAlpha }); },
                            restore() { Object.assign(this, stack.pop()); },
                            fillRect(...args) { rectangles.push({ color: this.fillStyle, alpha: this.globalAlpha, args }); },
                            fillText(text, x, y) { labels.push({ text, x, y }); },
                            drawImage() { assert.fail('A wall must not draw a piece asset'); },
                            measureText() { return { actualBoundingBoxLeft: 0, actualBoundingBoxAscent: 2, actualBoundingBoxDescent: 0 }; },
                        };
                        canvas.drawBoards(ctx, -3, 3, 0, 6);
                        const boardLayers = rectangles.slice(rectangles.findIndex(rect =>
                            rect.color === 'white-margin' || rect.color === 'black-margin'));
                        for (const pos of [wall, { ...wall, x: 3 }, { ...wall, x: 0, y: 0 }]) {
                            const [px, py] = canvas._getCoordinate(pos);
                            const layers = boardLayers.filter(({ args: [x, y, width, height] }) =>
                                px + 5 >= x && px + 5 < x + width && py + 5 >= y && py + 5 < y + height);
                            assert.equal(layers.length, 1, 'Only the board-margin layer should cover a wall');
                            assert.equal(layers[0].color, c ? 'black-margin' : 'white-margin');
                            assert.equal(layers[0].alpha, phantom ? 0.3 : 1);
                            assert(!labels.some(({ x, y }) => x >= px && x < px + 10 && y >= py && y < py + 10));
                            canvas.onClickSquare = canvas.onRightClickSquare = canvas.onPreviewSquare = () =>
                                assert.fail('Walls must not reach selection callbacks');
                            canvas.handleBoardClick(px + 5, py + 5, false);
                            canvas.handleBoardClick(px + 5, py + 5, true);
                            assert.equal(canvas.handleBoardPreview(px + 5, py + 5), null);
                        }
                        let selected;
                        canvas.onClickSquare = (...args) => { selected = args; };
                        const normal = { ...wall, x: 1 };
                        const [px, py] = canvas._getCoordinate(normal);
                        canvas.handleBoardClick(px + 5, py + 5, false);
                        assert.deepEqual(selected, [normal.l, normal.t, normal.c, normal.x, normal.y]);
                    }
                }
            }
        }
    }
});

function arrowCanvas(zoom, flipped = false) {
    const canvas = board(8, 8, flipped, false);
    canvas.infCanvas.viewportSize = { width: 500, height: 400 };
    canvas.infCanvas.cameraCurrent.getZoomLevel = () => zoom;
    canvas.data = { boards: [], highlights: [{ color: 'arrow', arrows: [{
        from: { l: 0, t: -1, c: true, x: 7, y: 7 },
        to: { l: 0, t: 2, c: true, x: 0, y: 7 },
    }] }] };
    canvas.colors = {};
    canvas.cameraElement = {};
    canvas.filterCache = { bounds: [] };
    canvas._resolveColor = color => color;
    const fills = [];
    let paths = 0, gradients = 0;
    canvas._drawArrow = () => paths++;
    const alphaStack = [];
    const ctx = {
        globalAlpha: 1,
        save() { alphaStack.push(this.globalAlpha); },
        restore() { this.globalAlpha = alphaStack.pop(); },
        fillRect() {}, fillText() {},
        measureText() { return { actualBoundingBoxAscent: 2 }; },
        fill() { fills.push(this.globalAlpha); },
        createLinearGradient() { gradients++; return { addColorStop() {} }; },
    };
    return { canvas, ctx, fills, work: () => ({ paths, gradients }) };
}

test('arrows fade by proportional endpoint distance at every canvas size, zoom and orientation', () => {
    for (const [width, height] of [[500, 400], [800, 600], [400, 500], [600, 800]]) {
        for (const zoom of [1, 2, 4]) {
            for (const flipped of [false, true]) {
                const { canvas, ctx, fills, work } = arrowCanvas(zoom, flipped);
                canvas.infCanvas.viewportSize = { width, height };
                const fadeDistance = 0.2 * Math.max(width, height);
                const arrow = canvas.data.highlights[0].arrows[0];
                const [fromX, fromY] = canvas._getCoordinate(arrow.from);
                const [toX] = canvas._getCoordinate(arrow.to);
                const leftEndpoint = Math.min(fromX, toX) + 5;
                const rightEndpoint = Math.max(fromX, toX) + 5;
                const render = distance => canvas.renderBoards(ctx, {
                    left: leftEndpoint + distance / 100 * fadeDistance / zoom,
                    right: rightEndpoint - distance / 100 * fadeDistance / zoom,
                    top: fromY, bottom: fromY + 10,
                });
                render(0);
                render(25);
                render(50);
                render(75);
                render(90);
                assert.equal(fills[0], 0.8);
                assert(Math.abs(fills[2] - 0.4) < 1e-12);
                assert(fills.every((alpha, i) => i === 0 || alpha < fills[i - 1]));
                const before = work();
                render(100);
                render(125);
                assert.deepEqual(work(), before, 'fully faded arrows create no paths or gradients');
                render(0);
                assert.equal(fills.at(-1), 0.8, 'returning to an endpoint restores opacity');
            }
        }
    }
});

test('resizing the canvas updates arrow opacity without changing the camera', () => {
    const { canvas, ctx, fills } = arrowCanvas(1);
    const bounds = { left: 75, right: 355, top: 0, bottom: 10 };
    canvas.renderBoards(ctx, bounds);
    assert.equal(fills.length, 0, '100 pixel distance reaches the original fade limit');
    canvas.infCanvas.viewportSize = { width: 1000, height: 400 };
    canvas.renderBoards(ctx, bounds);
    assert.equal(fills.at(-1), 0.4, '100 pixel distance is halfway through the resized fade range');
});

test('arrow candidates survive board cutoffs and refilter when the zoom margin changes', () => {
    const { canvas, ctx, fills } = arrowCanvas(1);
    // Both endpoint boards are outside the original grid filter; the nearer
    // endpoint is only 50 CSS pixels outside the viewport.
    canvas.renderBoards(ctx, { left: 25, right: 355, top: 0, bottom: 10 });
    assert.deepEqual(fills, [0.4]);
    canvas._filterBoardData(0, 1, 0, 4, 0, 0);
    assert.equal(canvas.filterCache.filteredArrowHighlight.arrow.length, 0);
    canvas._filterBoardData(0, 1, 0, 4, 1, 1);
    assert.equal(canvas.filterCache.filteredArrowHighlight.arrow.length, 1);
    canvas._filterBoardData(0, 1, 0, 4, 0, 0);
    assert.equal(canvas.filterCache.filteredArrowHighlight.arrow.length, 0);
});

test('a visible endpoint keeps full opacity and corner distance fades smoothly', () => {
    const { canvas, ctx, fills } = arrowCanvas(1);
    canvas.renderBoards(ctx, { left: -30, right: 200, top: 0, bottom: 100 });
    assert.equal(fills.at(-1), 0.8, 'one visible endpoint is enough');
    // The nearer endpoint is 30 pixels left and 40 pixels above the viewport.
    canvas.renderBoards(ctx, { left: 5, right: 200, top: 45, bottom: 100 });
    assert.equal(fills.at(-1), 0.4);
    // With both endpoints directly above the viewport, vertical distance alone
    // controls fading.
    canvas.renderBoards(ctx, { left: -30, right: 510, top: 55, bottom: 100 });
    assert.equal(fills.at(-1), 0.4);
});

test('opacity snapping preserves the agreed boundaries and rejects non-finite alpha', () => {
    for (const alpha of [0.002, 0.003, 0.5, 0.997, 0.998]) assert.equal(safeOpacity(alpha), alpha);
    for (const alpha of [-1, 0, 0.001999, NaN, Infinity, -Infinity]) assert.equal(safeOpacity(alpha), 0);
    for (const alpha of [0.998001, 1, 2]) assert.equal(safeOpacity(alpha), 1);
});

test('nearly transparent arrows skip paths and gradients and return smoothly above the cutoff', () => {
    const { canvas, ctx, fills, work } = arrowCanvas(1);
    const render = distance => canvas.renderBoards(ctx, {
        left: -25 + distance, right: 505 - distance, top: 0, bottom: 10
    });
    render(97);
    assert.equal(fills.length, 1);
    assert(fills[0] > 0.002);
    const before = work();
    // The fade multiplier is still above 0.002 here, but the final alpha after
    // multiplying by 0.8 is below it. Snap the final alpha, not the multiplier.
    render(97.2);
    render(99);
    render(100);
    assert.deepEqual(work(), before);
    assert.equal(fills.length, 1);
    render(97);
    assert.equal(fills.at(-1), fills[0]);
    assert.equal(fills.length, 2);
});

test('L/T labels skip near-zero opacity and snap near-one opacity to full', () => {
    const { canvas, ctx } = arrowCanvas(1);
    const labels = [];
    ctx.fillText = text => labels.push({ text, alpha: ctx.globalAlpha });
    const bounds = { left: 0, right: 100, top: 0, bottom: 100 };
    canvas.infCanvas.cameraCurrent.screenToWorld = () => ({ x: -4.5, y: -4.5 });
    canvas.renderBoards(ctx, bounds);
    assert.equal(labels.length, 0);
    canvas.infCanvas.cameraCurrent.screenToWorld = () => ({ x: 37.9, y: 37.9 });
    canvas.renderBoards(ctx, bounds);
    assert(labels.some(label => label.text.startsWith('L')));
    assert(labels.some(label => label.text.startsWith('T')));
    assert(labels.every(label => label.alpha === 1));
});

test('file/rank labels skip near-zero opacity and snap near-one opacity to full', () => {
    const { canvas, ctx } = arrowCanvas(3.01);
    canvas.data.boards = [{ l: 0, t: 0, c: false, fen: 'empty',
        parsed: Array.from({ length: 8 }, () => Array(8).fill(0)) }];
    canvas.infCanvas.cameraCurrent.screenToWorld = () => ({ x: -10, y: -10 });
    const labels = [];
    ctx.fillText = text => labels.push({ text, alpha: ctx.globalAlpha });
    const measureText = ctx.measureText;
    ctx.measureText = () => ({ ...measureText(), actualBoundingBoxLeft: 0, actualBoundingBoxDescent: 0 });
    const bounds = { left: 0, right: 100, top: 0, bottom: 100 };
    canvas.renderBoards(ctx, bounds);
    assert.equal(labels.length, 0);
    canvas.infCanvas.cameraCurrent.getZoomLevel = () => 4.99;
    canvas.renderBoards(ctx, bounds);
    assert.equal(labels.length, 16);
    assert(labels.every(label => label.alpha === 1));
});

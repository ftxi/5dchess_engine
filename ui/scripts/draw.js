// draw.js
// chess board specific implementation

import { InfiniteScrollableCanvas } from 'canvas';
import { nearestBoard, directionalTarget, focusViewBoard, heldNavigationSteps } from './navigation.js';

import { chooseLOD, SurfaceLOD, composeSurface } from 'level-of-detail';
import { loadColors, applyFadeToColors, applyFadeToColorString, resolveColor } from 'color';

// Snap near-endpoint alpha values to avoid intermittent Safari rendering flashes.
// Keep this separate from the easing curve and apply it after alpha scaling.
export function safeOpacity(alpha) {
    if (!Number.isFinite(alpha) || alpha < 0.002) return 0;
    if (alpha > 0.998) return 1;
    return alpha;
}

function distanceToBounds(x, y, bounds) {
    return Math.hypot(
        Math.max(bounds.left - x, 0, x - bounds.right),
        Math.max(bounds.top - y, 0, y - bounds.bottom)
    );
}

function smoothClamp(a, b, x) {
  if (x <= a) return 0;
  if (x >= b) return 1;
  const t = (x - a) / (b - a);
  const s = t * t * (3 - 2 * t);
  return s * 1;
}

export default class ChessBoardCanvas
{
    constructor(canvasId) {
        this.statusElement = document.getElementById('status');
        this.cameraElement = document.getElementById('camera');
        
        // Chess board configuration (actual values)
        this.boardLengthX = 8;
        this.boardLengthY = 8;
        this.squareSize = 10;
        this.boardMargin = 4;
        this.boardSkipX = 100;
        this.boardSkipY = 122;
        this.fontSizeforLT = 16;
        this.fontSizeforXY = 3;
        
        this.focusPoints = [{ l: 0, t: 0, c: 0 }];
        this.focusIndex = 0;
        
        this.flipped = false;
        this.doNotReverseFilesWhenFlipped = false;
        // Board data
        this.data = null;
        
        // Filtered/cached data for rendering optimization
        this.filterCache = {
            bounds: [null, null, null, null],
            filteredBoards: [],
            filteredCoordinateHighlight: {},
            filteredArrowHighlight: {},
            filteredTimelineHighlight: {},
            filteredBoardHighlight: {}
        };
        
        // Create infinite canvas
        this.infCanvas = new InfiniteScrollableCanvas(canvasId, {
            minZoom: -5.0,
            maxZoom: 7.0,
            initialZoom: -1.0
        });
        
        // Set up callbacks
        this.infCanvas.onRender = (ctx, bounds) => this.renderBoards(ctx, bounds);
        this.infCanvas.onClick = (x, y) => this.handleBoardClick(x, y, false);
        this.infCanvas.getDoubleTapTarget = (x, y) => {
            const board = this._boardAtWorldPoint(x, y, this.gestureData);
            return board ? `${board.x},${board.y}` : null;
        };
        this.infCanvas.onDoubleTap = (x, y) => this.zoomTappedBoard(x, y);
        this.infCanvas.onTapPreview = (x, y) => this.handleBoardPreview(x, y);
        this.infCanvas.onTapCommit = token => this.onCommitSquare(token);
        this.infCanvas.onTapCancel = token => this.onCancelSquare(token);
        this.infCanvas.onRightClick = (x, y) => this.handleBoardClick(x, y, true);
        this.infCanvas.onHover = (x, y) => this.updateStatus(x, y);
        
        this.infCanvas.startAnimation();
        this._loadColors();
        // Canvas text does not repaint automatically when a web font arrives.
        Promise.all([
            document.fonts.load(`italic ${this.fontSizeforXY}px Playfair`, 'abcdefgh12345678'),
            document.fonts.load(`${this.fontSizeforLT}px "Playfair Lining"`, 'LT-0123456789')
        ])
            .then(() => this.infCanvas.startAnimation())
            .catch(error => console.warn('Could not load the board coordinate font:', error));
    }

    _loadColors() {
        this.colors = loadColors();

        // Store originals so fade adjustments can be applied and reverted
        this._origColors = Object.assign({}, this.colors);

        // If an active fade is present, re-apply it after reloading base colors
        if (typeof this._activeFade === 'number' && this._activeFade > 0) {
            this._applyFadeToColors(this._activeFade);
        }
    }

    _resolveColor(token) {
        return resolveColor(token, this._activeFade);
    }

    _applyFadeToColorString(colorStr, fade) {
        return applyFadeToColorString(colorStr, fade);
    }

    _applyFadeToColors(fade) {
        if (!this._origColors) this._origColors = Object.assign({}, this.colors);
        this.colors = applyFadeToColors(this._origColors, fade);
        this._activeFade = fade;
    }

    _clearFade() {
        if (this._origColors) {
            this.colors = Object.assign({}, this._origColors);
        }
        this._activeFade = null;
    }

    setFlipped(flipped) {
        if (this.flipped === flipped) {
            return;
        }

        this.flipped = flipped;
        if (this.data) this._prepareBoardRasters(this.data.boards);

        // Invalidate visibility cache and remap camera Y through the same flip transform used by board centers:
        // worldY' = -worldY + boardPixelHeight
        // cameraY' = -cameraY - boardPixelHeight
        const boardPixelHeight = this.boardLengthY * this.squareSize;
        this.filterCache.bounds = [null, null, null, null, null];
        this.infCanvas.cameraTarget.y = -this.infCanvas.cameraTarget.y - boardPixelHeight;
        this.infCanvas.cameraCurrent.y = -this.infCanvas.cameraCurrent.y - boardPixelHeight;
        this.infCanvas.startAnimation();
    }

    setDoNotReverseFilesWhenFlipped(value) {
        this.doNotReverseFilesWhenFlipped = value;
        if (this.data) this._prepareBoardRasters(this.data.boards);
        this.infCanvas.startAnimation();
    }

    // The same reflection maps logical files to display columns and back.
    _displayBoardCol(col) {
        return this.flipped && !this.doNotReverseFilesWhenFlipped
            ? this.boardLengthX - 1 - col : col;
    }

    // Map logical timeline index to the displayed timeline index.
    _displayL(l) {
        return this.flipped ? -l : l;
    }

    // Map board-array row (top-origin) to the displayed row on canvas.
    _displayBoardRow(row) {
        return this.flipped ? (this.boardLengthY - 1 - row) : row;
    }

    // Map chess coordinate y (bottom-origin) to the displayed row on canvas.
    _displayCoordYRow(y) {
        return this.flipped ? y : (this.boardLengthY - 1 - y);
    }

    // Rank text associated with a board-array row index.
    _displayRankForRow(row) {
        return this.boardLengthY - row;
    }

    // Choose contrasting label color based on the underlying square color.
    _getLabelTextColor(boardRow, boardCol) {
        const isWhiteSquare = ((boardRow + boardCol) % 2) === 0;
        return isWhiteSquare ? this.colors.squareBlack : this.colors.squareWhite;
    }

    // Convert displayed L bounds back to logical L bounds for filtering data.
    _actualLRangeFromDisplay(displayLMin, displayLMax) {
        if (!this.flipped) {
            return [displayLMin, displayLMax];
        }
        return [-displayLMax, -displayLMin];
    }

    // Set the board data (replaces global data variable)
    setData(data) {
        this.data = data;
        // Handle optional fade property (0..1): reduces saturation of all canvas colors
        if (data && typeof data.fade === 'number') {
            const v = Math.max(0, Math.min(1, data.fade));
            this._applyFadeToColors(v);
        } else {
            this._clearFade();
        }
        
        // Update board dimensions if provided
        if (data.size) {
            this.boardLengthX = data.size.x;
            this.boardLengthY = data.size.y;
            this.boardSkipX = this.boardLengthX * this.squareSize + 20;
            this.boardSkipY = Math.max(
                this.boardLengthY * this.squareSize + 20,
                Math.floor(this.boardSkipX * 1.12)
            );
        }

        this.backgroundShiftX = (this.boardSkipX - this.squareSize * this.boardLengthX) / 2;
        this.backgroundShiftY = (this.boardSkipY - this.squareSize * this.boardLengthY) / 2;

        if(data.focus)
        {
            this.focusPoints = data.focus;
        }
        this._prepareBoardRasters(data.boards);
        // Clear cache when data changes
        this.filterCache.bounds = [null, null, null, null];
        
        this.infCanvas.startAnimation();
    }

    // Filter board data based on visible bounds (with caching)
    _filterBoardData(displayLMin, displayLMax, vMin, vMax, arrowMarginL = 0, arrowMarginV = 0) {
        const cache = this.filterCache;
        const bounds = [displayLMin, displayLMax, vMin, vMax, this.flipped, arrowMarginL, arrowMarginV];
        const [lMin, lMax] = this._actualLRangeFromDisplay(displayLMin, displayLMax);
        const [arrowLMin, arrowLMax] = this._actualLRangeFromDisplay(
            displayLMin - arrowMarginL, displayLMax + arrowMarginL
        );
        
        // Check if cache is still valid
        if (cache.bounds.length === bounds.length && cache.bounds.every((v, i) => v === bounds[i])) {
            return; // Cache is valid, no need to refilter
        }
        
        // Update cache
        cache.bounds = bounds;
        
        if (!this.data) return;
        
        // Filter boards
        cache.filteredBoards = this.data.boards.filter((board) => {
            let l = board.l, v = board.t << 1 | board.c;
            return !(l < lMin || l > lMax || v < vMin || v > vMax || board.fen == null);
        });
        
        // Reset highlight caches
        cache.filteredCoordinateHighlight = {};
        cache.filteredArrowHighlight = {};
        cache.filteredTimelineHighlight = {};
        cache.filteredBoardHighlight = {};
        
        // Filter highlights if they exist
        if (this.data.highlights || this.selectionHighlights) {
            for (let colorBlock of [...(this.data.highlights || []), ...(this.selectionHighlights || [])]) {
                const color = this._resolveColor(colorBlock.color) || colorBlock.color;
                
                // Filter coordinate highlights
                if (colorBlock.coordinates) {
                    cache.filteredCoordinateHighlight[color] = (cache.filteredCoordinateHighlight[color] || []).concat(colorBlock.coordinates.filter((pos) => {
                        let l = pos.l, v = pos.t << 1 | pos.c;
                        return !(l < lMin || l > lMax || v < vMin || v > vMax);
                    }));
                }
                
                // Filter arrow highlights
                if (colorBlock.arrows) {
                    cache.filteredArrowHighlight[color] = colorBlock.arrows.filter((pos) => {
                        let l1 = pos.from.l, v1 = pos.from.t << 1 | pos.from.c;
                        let l2 = pos.to.l, v2 = pos.to.t << 1 | pos.to.c;
                        return !(l1 < arrowLMin || l1 > arrowLMax || v1 < vMin - arrowMarginV || v1 > vMax + arrowMarginV) ||
                               !(l2 < arrowLMin || l2 > arrowLMax || v2 < vMin - arrowMarginV || v2 > vMax + arrowMarginV);
                    });
                }
                
                // Filter timeline highlights
                if (colorBlock.timelines) {
                    cache.filteredTimelineHighlight[color] = colorBlock.timelines.filter((l) => {
                        return !(l < lMin || l > lMax);
                    });
                }
                
                // Filter board highlights
                if (colorBlock.boards) {
                    cache.filteredBoardHighlight[color] = colorBlock.boards.filter((pos) => {
                        let l = pos.l, v = pos.t << 1 | pos.c;
                        return !(l < lMin || l > lMax || v < vMin || v > vMax);
                    });
                }
            }
        }
    }

    // Helper: convert board position to pixel coordinate
    _getCoordinate(pos) {
        const l = this._displayL(pos.l);
        const v = pos.t << 1 | pos.c;
        const shiftX = v * this.boardSkipX;
        const shiftY = l * this.boardSkipY;
        const row = this._displayCoordYRow(pos.y);
        return [
            this._displayBoardCol(pos.x) * this.squareSize + shiftX,
            row * this.squareSize + shiftY
        ];
    }

    _timelineLabelBaseline(ctx, l) {
        const ascent = ctx.measureText('L').actualBoundingBoxAscent;
        return l * this.boardSkipY - this.backgroundShiftY + ascent;
    }

    // Shared primitives for direct rendering and cached board images. Coordinates
    // are local to the board origin; callers supply placement and raster scale.
    _drawBoardMargin(ctx, board, shiftX, shiftY, color) {
        ctx.save();
        ctx.fillStyle = color ?? ((board.c == 1) ? this.colors.boardMarginBlack : this.colors.boardMarginWhite);
        ctx.fillRect(
            shiftX - this.boardMargin,
            shiftY - this.boardMargin,
            this.boardLengthX * this.squareSize + 2 * this.boardMargin,
            this.boardLengthY * this.squareSize + 2 * this.boardMargin
        );
        ctx.restore();
    }

    _drawBoardSquares(ctx, board, shiftX, shiftY) {
        ctx.save();
        const hasWalls = board.parsed?.some(row => row.includes('#'));
        ctx.fillStyle = this.colors.squareBlack;
        if (!hasWalls) {
            ctx.fillRect(shiftX, shiftY, this.boardLengthX * this.squareSize, this.boardLengthY * this.squareSize);
        }

        for (let row = 0; row < this.boardLengthY; row++) {
            for (let col = 0; col < this.boardLengthX; col++) {
                if (board.parsed?.[row]?.[col] === '#') continue;
                if (hasWalls || (row + col) % 2 === 0) {
                    ctx.fillStyle = (row + col) % 2 === 0 ? this.colors.squareWhite : this.colors.squareBlack;
                    const drawRow = this._displayBoardRow(row);
                    ctx.fillRect(
                        this._displayBoardCol(col) * this.squareSize + shiftX,
                        drawRow * this.squareSize + shiftY,
                        this.squareSize,
                        this.squareSize
                    );
                }
            }
        }
        ctx.restore();
    }

    _drawBoardSurface(ctx, board, shiftX, shiftY, squareLength, highlights = []) {
        this._drawBoardMargin(ctx, board, shiftX, shiftY);
        for (const color of highlights) {
            this._drawBoardMargin(ctx, board, shiftX, shiftY, color);
        }
        this._drawBoardSquares(ctx, board, shiftX, shiftY);
        if (squareLength > 30) this._drawBoardLabels(ctx, board, shiftX, shiftY, squareLength);
    }

    _drawBoardLabels(ctx, board, shiftX, shiftY, squareLength) {
        const labelOpacity = safeOpacity(smoothClamp(30, 50, squareLength));
        if (!labelOpacity) return;
        ctx.save();
        ctx.globalAlpha *= labelOpacity;
        ctx.font = `italic ${this.fontSizeforXY}px Playfair, serif`;
        ctx.textBaseline = 'alphabetic';
        ctx.textAlign = 'left';
        const labelInsetX = this.fontSizeforXY * 0.05;
        const rankInsetY = this.fontSizeforXY * 0.1;
        const rankLabels = Array.from({ length: this.boardLengthY }, (_, row) => {
            const text = `${this._displayRankForRow(row)}`;
            const metrics = ctx.measureText(text);
            return { text, x: labelInsetX + metrics.actualBoundingBoxLeft,
                y: rankInsetY + metrics.actualBoundingBoxAscent };
        });
        const fileLabels = Array.from({ length: this.boardLengthX }, (_, i) => {
            const text = String.fromCharCode(97 + i);
            return { text, x: labelInsetX + ctx.measureText(text).actualBoundingBoxLeft };
        });
        const fileDescent = Math.max(0, ctx.measureText(fileLabels.map(label => label.text).join('')).actualBoundingBoxDescent);
        const bottomBoardRow = this.flipped ? 0 : this.boardLengthY - 1;
        for (const parity of [1, 0]) {
            for (let i = parity; i < this.boardLengthX; i += 2) {
                if (board.parsed?.[bottomBoardRow]?.[i] === '#') continue;
                ctx.fillStyle = this._getLabelTextColor(bottomBoardRow, i);
                ctx.fillText(fileLabels[i].text,
                    shiftX + this._displayBoardCol(i) * this.squareSize + fileLabels[i].x,
                    shiftY + this.boardLengthY * this.squareSize - fileDescent);
            }
            for (let i = 1 - parity; i < this.boardLengthY; i += 2) {
                if (board.parsed?.[i]?.[this._displayBoardCol(0)] === '#') continue;
                ctx.fillStyle = this._getLabelTextColor(i, this._displayBoardCol(0));
                ctx.fillText(rankLabels[i].text, shiftX + rankLabels[i].x,
                    shiftY + this._displayBoardRow(i) * this.squareSize + rankLabels[i].y);
            }
        }
        ctx.restore();
    }

    _boardRasterKey(board) {
        const walls = board.parsed?.map(row => row.map(cell => cell === '#' ? '#' : '.').join('')).join('/');
        return JSON.stringify([this.boardLengthX, this.boardLengthY, this.squareSize, this.boardMargin,
            this.flipped, this.doNotReverseFilesWhenFlipped, Boolean(board.c), walls,
            this.colors.boardMarginWhite, this.colors.boardMarginBlack,
            this.colors.squareWhite, this.colors.squareBlack]);
    }

    // Early import data is only used to prepare surfaces, never to replace the
    // displayed game. Both frame colors are needed even for a single-board setup.
    async prepareBoardRasters(data, isCurrent = () => true) {
        if (!this.boardLOD) this.boardLOD = new SurfaceLOD();
        const renderer = Object.create(this);
        renderer.boardLengthX = data.size.x;
        renderer.boardLengthY = data.size.y;
        renderer.colors = this._origColors || this.colors;
        const prepared = new Set();
        for (const board of data.boards) {
            if (!isCurrent()) return;
            const key = renderer._boardRasterKey(board);
            if (prepared.has(key)) continue;
            prepared.add(key);
            renderer._prepareBoardRasters([board]);
            // Yield between layouts so large custom setups do not block input.
            await new Promise(resolve => setTimeout(resolve, 0));
        }
    }

    _prepareBoardRasters(boards) {
        if (!this.boardLOD) this.boardLOD = new SurfaceLOD();
        const width = this.boardLengthX * this.squareSize + 2 * this.boardMargin;
        const height = this.boardLengthY * this.squareSize + 2 * this.boardMargin;
        for (const board of boards) {
            for (const c of [false, true]) {
                const surfaceBoard = { ...board, c };
                this.boardLOD.prepare(this._boardRasterKey(surfaceBoard), width, height, this.squareSize,
                    (ctx, pixelWidth, pixelHeight) => {
                        ctx.scale(pixelWidth / width, pixelHeight / height);
                        // No labels, pieces, highlights, or phantom opacity in LODs.
                        this._drawBoardSurface(ctx, surfaceBoard, this.boardMargin, this.boardMargin, 1);
                    });
            }
        }
    }

    _drawNormalBoard(ctx, board, squareLength, highlights, bounds) {
        const x = (board.t * 2 + Number(board.c)) * this.boardSkipX;
        const y = this._displayL(board.l) * this.boardSkipY;
        const opacity = board.isPhantom ? this.colors.phantomBoardOpacity : 1;
        if (opacity === 0) return;
        if (opacity === 1) {
            this._drawBoardSurface(ctx, board, x, y, squareLength, highlights);
        } else {
            composeSurface(ctx,
                this.boardLengthX * this.squareSize + 2 * this.boardMargin,
                this.boardLengthY * this.squareSize + 2 * this.boardMargin,
                squareLength / this.squareSize * this.infCanvas.pixelRatio,
                x - this.boardMargin, y - this.boardMargin, opacity,
                surface => this._drawBoardSurface(surface, board, this.boardMargin, this.boardMargin,
                    squareLength, highlights), bounds);
        }
    }

    // Main drawing method
    drawBoards(ctx, lMin, lMax, vMin, vMax, bounds = null) {
        if (!this.data) return;
        
        // Update camera status display
        if (this.cameraElement) {
            this.cameraElement.innerHTML = `(L${lMin}V${vMin}) -- (L${lMax}V${vMax})`;
        }
        
        // Filter data based on visible bounds (with caching)
        const zoom = this.infCanvas.cameraCurrent.getZoomLevel();
        const { width, height } = this.infCanvas.viewportSize;
        const fadeDistance = 0.2 * Math.max(width, height) / zoom;
        this._filterBoardData(lMin, lMax, vMin, vMax,
            bounds ? Math.ceil(fadeDistance / this.boardSkipY) : 0,
            bounds ? Math.ceil(fadeDistance / this.boardSkipX) : 0);
        
        const cache = this.filterCache;
        const squareLength = zoom * this.squareSize;
        
        // Layer 1: Background grid on multiverse
        ctx.fillStyle = this.colors.spGridWhite;
        ctx.fillRect(
            vMin * this.boardSkipX - this.backgroundShiftX,
            lMin * this.boardSkipY - this.backgroundShiftY,
            (vMax - vMin + 1) * this.boardSkipX,
            (lMax - lMin + 1) * this.boardSkipY
        );
        
        ctx.fillStyle = this.colors.spGridBlack;
        for (let l = lMin - 1; l <= lMax; l++) {
            for (let t = vMin >> 1; t <= vMax >> 1; t++) {
                if ((l + t) % 2 === 0) {
                    ctx.fillRect(
                        t * 2 * this.boardSkipX - this.backgroundShiftX,
                        l * this.boardSkipY - this.backgroundShiftY,
                        2 * this.boardSkipX,
                        this.boardSkipY
                    );
                }
            }
        }

        // Layer 1.1: L/T numbers
        
        const opacityValues = (() => {
            const camera = this.infCanvas.cameraCurrent;
            const zoomLevel = camera.getZoomLevel();
            const {x:worldX, y:worldY} = camera.screenToWorld(
                (2 * this.boardSkipX - this.backgroundShiftX) * zoomLevel,
                (this.boardSkipY - this.backgroundShiftY) * zoomLevel + 80, // 80 is the approximate height of hud
                this.infCanvas.viewportSize.width,
                this.infCanvas.viewportSize.height
            );
            const {l, t} = this.worldToLT(worldX, worldY);
            const deltaX = Math.min(
                worldX - (t * 2 * this.boardSkipX - this.backgroundShiftX),
                (t+1) * 2 * this.boardSkipX - this.backgroundShiftX - worldX
            );
            const deltaY = Math.min(
                worldY - (l * this.boardSkipY - this.backgroundShiftY),
                (l+1) * this.boardSkipY - this.backgroundShiftY - worldY
            );
            const lOpacity = safeOpacity(smoothClamp(
                this.fontSizeforLT / 3.0,
                3 * this.fontSizeforLT,
                deltaX * zoomLevel
            ));
            const tOpacity = safeOpacity(smoothClamp(
                this.fontSizeforLT / 3.0,
                3 * this.fontSizeforLT,
                deltaY * zoomLevel
            ));
            return {l, t, lOpacity, tOpacity};
        })();
        
        ctx.font = `${this.fontSizeforLT}px "Playfair Lining", serif`;
        ctx.textBaseline = 'alphabetic';
        ctx.save();
        if(opacityValues.lOpacity > 0)
        {
            ctx.globalAlpha = opacityValues.lOpacity;
            ctx.fillStyle = this.colors.spGridWhite;
            for (let l = lMin - 1; l <= lMax; l++) {
                let t = opacityValues.t;
                if ((l + t) % 2 === 0) {
                    ctx.fillText(
                        `L${this._displayL(l)}`,
                        t * 2 * this.boardSkipX - this.backgroundShiftX - 2.4,
                        this._timelineLabelBaseline(ctx, l)
                    );
                }
            }
            ctx.fillStyle = this.colors.spGridBlack;
            for (let l = lMin - 1; l <= lMax; l++) {
                let t = opacityValues.t;
                if ((l + t) % 2 !== 0) {
                    ctx.fillText(
                        `L${this._displayL(l)}`,
                        t * 2 * this.boardSkipX - this.backgroundShiftX - 2.4,
                        this._timelineLabelBaseline(ctx, l)
                    );
                }
            }
        }
        
        if(opacityValues.tOpacity > 0)
        {
            ctx.globalAlpha = opacityValues.tOpacity;
            ctx.fillStyle = this.colors.spGridWhite;
            for (let t = vMin >> 1; t <= vMax >> 1; t++) {
                let l = opacityValues.l;
                if ((l + t) % 2 !== 0) {
                    ctx.fillText(
                        `T${t}`,
                        t * 2 * this.boardSkipX - this.backgroundShiftX - 1,
                        l * this.boardSkipY - this.backgroundShiftY
                    );
                }
            }
            ctx.fillStyle = this.colors.spGridBlack;
            for (let t = vMin >> 1; t <= vMax >> 1; t++) {
                let l = opacityValues.l;
                if ((l + t) % 2 === 0) {
                    ctx.fillText(
                        `T${t}`,
                        t * 2 * this.boardSkipX - this.backgroundShiftX - 1,
                        l * this.boardSkipY - this.backgroundShiftY
                    );
                }
            }
        }
        ctx.restore();
        
        // Layer 1.3: Present column
        if (this.data.present) {
            let t = this.data.present.t, c = this.data.present.c;
            let color = this._resolveColor(this.data.present.color) || this.colors.present;
            ctx.fillStyle = color;
            ctx.fillRect(
                (t * 2 + c) * this.boardSkipX - this.backgroundShiftX,
                lMin * this.boardSkipY - this.backgroundShiftY,
                this.boardSkipX,
                (lMax - lMin + 1) * this.boardSkipY
            );
        }
        
        const boardHighlights = new Map();
        const surfacePositions = new Set(cache.filteredBoards.map(
            board => `${board.l},${board.t},${Number(board.c)}`));
        for (const [color, positions] of Object.entries(cache.filteredBoardHighlight)) {
            for (const pos of positions) {
                const key = `${pos.l},${pos.t},${Number(pos.c)}`;
                // Highlights may also mark an empty grid position with no board.
                if (!surfacePositions.has(key)) {
                    this._drawBoardMargin(ctx, pos,
                        (pos.t * 2 + Number(pos.c)) * this.boardSkipX,
                        this._displayL(pos.l) * this.boardSkipY, color);
                }
                if (!boardHighlights.has(key)) boardHighlights.set(key, []);
                boardHighlights.get(key).push(color);
            }
        }
        for (const board of cache.filteredBoards) {
            const highlights = boardHighlights.get(`${board.l},${board.t},${Number(board.c)}`) || [];
            const image = !highlights.length && squareLength <= 30
                ? this.boardLOD?.get(this._boardRasterKey(board), squareLength * this.infCanvas.pixelRatio) : null;
            if (image) {
                ctx.save();
                if (board.isPhantom) ctx.globalAlpha *= this.colors.phantomBoardOpacity;
                ctx.drawImage(image,
                    (board.t * 2 + Number(board.c)) * this.boardSkipX - this.boardMargin,
                    this._displayL(board.l) * this.boardSkipY - this.boardMargin,
                    this.boardLengthX * this.squareSize + 2 * this.boardMargin,
                    this.boardLengthY * this.squareSize + 2 * this.boardMargin);
                ctx.restore();
            } else {
                this._drawNormalBoard(ctx, board, squareLength, highlights, bounds);
            }
        }

        this.cameraElement.innerHTML = `SquareLength: ${squareLength.toFixed(2)}`;

        // Layer 3: Highlighted squares
        ctx.save();
        //ctx.globalAlpha = 0.5;
        for (let color in cache.filteredCoordinateHighlight) {
            ctx.fillStyle = color;
            for (let pos of cache.filteredCoordinateHighlight[color]) {
                if (this.isWall(pos)) continue;
                ctx.fillRect(...this._getCoordinate(pos), this.squareSize, this.squareSize);
            }
        }
        ctx.restore();
        
        if(squareLength > 0.7) {
            // Layer 4: Pieces
            const imgs = chooseLOD(squareLength * this.infCanvas.pixelRatio);
            for (const board of cache.filteredBoards) {
                let l = this._displayL(board.l), v = board.t << 1 | board.c;
                const shiftX = v * this.boardSkipX;
                const shiftY = l * this.boardSkipY;
                
                let parsedBoard = board.parsed;
                for (let row = 0; row < this.boardLengthY; row++) {
                    for (let col = 0; col < this.boardLengthX; col++) {
                        const piece = parsedBoard[row][col];
                        if (isNaN(piece) && imgs[piece]) {
                            const drawRow = this._displayBoardRow(row);
                            ctx.drawImage(
                                imgs[piece],
                                this._displayBoardCol(col) * this.squareSize + shiftX,
                                drawRow * this.squareSize + shiftY,
                                this.squareSize,
                                this.squareSize
                            );
                        }
                    }
                }
            }
        }
        
        // Layer 5: Arrows
        for (let color in cache.filteredArrowHighlight) {
            ctx.save();
            ctx.globalAlpha = 0.8;
            ctx.lineWidth = 1;
            
            for (let arrow of cache.filteredArrowHighlight[color]) {
                const [fromX, fromY] = this._getCoordinate(arrow.from);
                const [toX, toY] = this._getCoordinate(arrow.to);
                const r = this.squareSize / 2;
                const fromDistance = bounds ? distanceToBounds(fromX + r, fromY + r, bounds) : 0;
                const toDistance = bounds ? distanceToBounds(toX + r, toY + r, bounds) : 0;
                const distance = Math.min(fromDistance, toDistance);
                const opacity = 1 - smoothClamp(0, fadeDistance, distance);
                const alpha = safeOpacity(0.8 * opacity);
                if (alpha === 0) continue;
                ctx.save();
                ctx.globalAlpha = alpha;
                
                this._drawArrow(ctx, fromX + r, fromY + r, toX + r, toY + r);
                
                const gradient = ctx.createLinearGradient(fromX + r, fromY + r, toX + r, toY + r);
                let u = this.boardLengthX / 2.0 / Math.sqrt((fromX - toX) ** 2 + (fromY - toY) ** 2);
                gradient.addColorStop(0, 'rgba(0,0,0,0)');
                gradient.addColorStop(u / 3, 'rgba(0,0,0,0)');
                gradient.addColorStop(u, this.colors.arrowWhiteTop);
                gradient.addColorStop(1, color);
                
                ctx.fillStyle = gradient;
                ctx.fill();
                
                ctx.restore();
            }
            ctx.restore();
        }
    }

        // Arrow drawing helper
    _drawArrow(ctx, fromX, fromY, toX, toY) {
        const headlen = 8; // length of head in pixels
        const width = 3 / 2; // width of the arrow line
        const tipSpan = Math.PI / 7; // angle between arrow line and arrow tip side
        
        const dx = toX - fromX;
        const dy = toY - fromY;
        const angle = Math.atan2(dy, dx);
        
        // Calculate the angles for the arrowhead
        const leftAngle = angle - tipSpan;
        const rightAngle = angle + tipSpan;
        
        // Coordinates of the triangle tip
        const leftX = toX - headlen * Math.cos(leftAngle);
        const leftY = toY - headlen * Math.sin(leftAngle);
        const rightX = toX - headlen * Math.cos(rightAngle);
        const rightY = toY - headlen * Math.sin(rightAngle);
        const midX = toX - headlen * Math.cos(tipSpan) * Math.cos(angle);
        const midY = toY - headlen * Math.cos(tipSpan) * Math.sin(angle);
        
        // Draw the arrow
        ctx.beginPath();
        ctx.moveTo(fromX - width * Math.sin(angle), fromY + width * Math.cos(angle));
        ctx.lineTo(fromX + width * Math.sin(angle), fromY - width * Math.cos(angle));
        ctx.lineTo(midX + width * Math.sin(angle), midY - width * Math.cos(angle));
        ctx.lineTo(rightX, rightY); // Right side of the triangle
        ctx.lineTo(toX, toY); // Tip of the arrow
        ctx.lineTo(leftX, leftY); // Left side of the triangle
        ctx.lineTo(midX - width * Math.sin(angle), midY + width * Math.cos(angle));
        ctx.closePath(); // Close the path to form a triangle
    }

    renderBoards(ctx, bounds) {
        // Calculate which boards are visible
        const lMin = Math.floor(bounds.top / this.boardSkipY);
        const vMin = Math.floor(bounds.left / this.boardSkipX);
        const lMax = Math.ceil(bounds.bottom / this.boardSkipY);
        const vMax = Math.ceil(bounds.right / this.boardSkipX);
        
        // Call the drawing method (can be overridden)
        this.drawBoards(ctx, lMin, lMax, vMin, vMax, bounds);
        
        /*
        // Debug: origin indicator
        ctx.fillStyle = this.colors.debugRed;
        ctx.fillRect(0, 0, 10, 10);
        */
    }

    /**
     * Returns the board L/T grid coordinates for the given world X/Y position.
     * @param {number} worldX 
     * @param {number} worldY 
     * @returns object with properties l and t
     */
    worldToLT(worldX, worldY) {
        const l = Math.floor((worldY + this.backgroundShiftY) / this.boardSkipY);
        const t = Math.floor((worldX + this.backgroundShiftX) / this.boardSkipX / 2);
        return { l, t };
    }

    worldToBoard(worldX, worldY) {
        const displayL = Math.floor(worldY / this.boardSkipY);
        const l = this._displayL(displayL);
        const v = Math.floor(worldX / this.boardSkipX);
        const c = (v & 1) !== 0;
        const t = v >> 1;
        const col = Math.floor((worldX - v * this.boardSkipX) / this.squareSize);
        const x = this._displayBoardCol(col);
        const row = Math.floor((worldY - displayL * this.boardSkipY) / this.squareSize);
        const y = this.flipped ? row : (this.boardLengthY - 1 - row);
        
        return { l, t, c, x, y };
    }

    handleBoardClick(worldX, worldY, isRightClick) {
        const pos = this.worldToBoard(worldX, worldY);
        if (this.isWall(pos)) return;

        if (pos.x >= 0 && pos.x < this.boardLengthX && pos.y >= 0 && pos.y < this.boardLengthY) {
            // Call the appropriate callback method
            if (isRightClick) {
                this.onRightClickSquare(pos.l, pos.t, pos.c, pos.x, pos.y);
            } else {
                this.onClickSquare(pos.l, pos.t, pos.c, pos.x, pos.y);
            }
            
            const square = String.fromCharCode(97 + pos.x) + (pos.y + 1);
            this.updateStatusText(`${isRightClick ? 'right' : 'left'} click at (${pos.l}T${pos.t}${pos.c ? 'b' : 'w'})${square}`);
        }
    }

    isWall(pos) {
        const board = this.data?.boards?.find(board => board.l === pos.l
            && board.t === pos.t && Boolean(board.c) === Boolean(pos.c));
        return board?.parsed?.[this.boardLengthY - 1 - pos.y]?.[pos.x] === '#';
    }

    handleBoardPreview(worldX, worldY) {
        const pos = this.worldToBoard(worldX, worldY);
        return this.isWall(pos) ? null : this.onPreviewSquare(pos);
    }

    // Override these methods in your implementation
    onClickSquare(l, t, c, x, y) {
        // Placeholder - override this method
        console.log('Left click:', { l, t, c, x, y });
        //report_click(l, t, c, x, y);
    }

    onPreviewSquare(pos) { return pos; }
    onCommitSquare(pos) {
        if (pos) this.onClickSquare(pos.l, pos.t, pos.c, pos.x, pos.y);
    }
    onCancelSquare() {}

    setSelectionHighlights(highlights) {
        this.selectionHighlights = highlights;
        this.filterCache.bounds = [null, null, null, null];
        this.infCanvas.startAnimation();
    }

    onRightClickSquare(l, t, c, x, y) {
        // Placeholder - override this method
        console.log('Right click:', { l, t, c, x, y });
        //report_right_click(l, t, c, x, y);
    }

    updateStatus(worldX, worldY) {
        this.updateStatusText(`x = ${worldX.toFixed(2)} y = ${worldY.toFixed(2)}`);
    }

    updateStatusText(text) {
        if (this.statusElement) {
            this.statusElement.innerHTML = text;
        }
    }

    _focusScale(wide = false) {
        const canvas = this.infCanvas.viewportSize;
        const actualScale = document.body.classList.contains('compact-layout')
            ? Math.min(canvas.width / (this.boardLengthX * this.squareSize + 40),
                       canvas.height / (this.boardLengthY * this.squareSize + 40))
            : canvas.width / 120 / 3;
        if (wide) {
            // Fit a full neighborhood, including empty cells, in the visible canvas.
            const boardWidth = this.boardLengthX * this.squareSize;
            const boardHeight = this.boardLengthY * this.squareSize;
            return Math.log2(Math.min(
                (canvas.width - 48) / (2 * this.boardSkipX + boardWidth),
                (canvas.height - 48) / (2 * this.boardSkipY + boardHeight),
                actualScale * 0.4
            ));
        }
        return Math.log2(actualScale);
    }

    _navigationBoards(data = this.data) {
        return (data?.boards || [])
            .filter(board => board.fen != null)
            .map(board => ({ x: 2 * board.t + Number(board.c), y: this._displayL(board.l) }));
    }

    _cameraGridPosition(camera) {
        return {
            x: (-camera.x - this.boardLengthX * this.squareSize / 2) / this.boardSkipX,
            y: (-camera.y - this.boardLengthY * this.squareSize / 2) / this.boardSkipY
        };
    }

    _moveToBoard(board, scale = null) {
        this.infCanvas.moveTo(
            board.x * this.boardSkipX + this.boardLengthX * this.squareSize / 2,
            board.y * this.boardSkipY + this.boardLengthY * this.squareSize / 2,
            scale
        );
    }

    _boardAtWorldPoint(worldX, worldY, data = this.data) {
        const pos = this.worldToBoard(worldX, worldY);
        if (pos.x < 0 || pos.x >= this.boardLengthX || pos.y < 0 || pos.y >= this.boardLengthY) return null;
        return this._navigationBoards(data).find(board => board.x === 2 * pos.t + Number(pos.c)
            && board.y === this._displayL(pos.l)) || null;
    }

    _focusedBoard(boards) {
        const spacing = { x: this.boardSkipX, y: this.boardSkipY };
        const size = { x: this.boardLengthX * this.squareSize, y: this.boardLengthY * this.squareSize };
        const normalScale = this._focusScale();
        const current = this.infCanvas.cameraCurrent, target = this.infCanvas.cameraTarget;
        const moving = ['x', 'y', 'scale'].some(axis => current[axis] !== target[axis]);
        return focusViewBoard(boards, moving ? target : current, spacing, size, normalScale);
    }

    zoomTappedBoard(worldX, worldY) {
        const board = this._boardAtWorldPoint(worldX, worldY);
        if (board) this._moveToBoard(board, this._focusScale(Boolean(this._focusedBoard([board]))));
    }

    zoomNearestBoard({ wide = false } = {}) {
        const boards = this._navigationBoards();
        const spacing = { x: this.boardSkipX, y: this.boardSkipY };
        const focused = this._focusedBoard(boards);
        const board = focused || nearestBoard(boards,
            this._cameraGridPosition(this.infCanvas.cameraCurrent), spacing);
        if (!board) return;
        this._moveToBoard(board, this._focusScale(wide || Boolean(focused)));
    }

    navigateBoard(direction, options = {}) {
        const directions = {
            up: { x: 0, y: -1 }, left: { x: -1, y: 0 },
            down: { x: 0, y: 1 }, right: { x: 1, y: 0 }
        };
        const vector = options.direction || directions[direction];
        if (!vector || (!vector.x && !vector.y)) return;
        let steps = 1;
        if (options.held) {
            const key = `${options.session}:${vector.x}:${vector.y}`;
            const result = heldNavigationSteps({
                direction: vector, spacing: { x: this.boardSkipX, y: this.boardSkipY },
                zoom: this.infCanvas.cameraCurrent.getZoomLevel(),
                referenceZoom: 2 ** this._focusScale(), rate: options.rate, elapsed: options.elapsed,
                remainder: key === this.heldNavigationKey ? this.heldNavigationRemainder : 0
            });
            this.heldNavigationKey = key;
            this.heldNavigationRemainder = result.remainder;
            steps = result.steps;
        } else {
            this.heldNavigationKey = null;
            this.heldNavigationRemainder = 0;
        }
        if (!steps) return;
        const target = directionalTarget(this._navigationBoards(),
            this._cameraGridPosition(this.infCanvas.cameraTarget), vector, steps);
        this._moveToBoard(target);
    }

    goToNextFocus() {
        if (!this.focusPoints.length) return;
        this.focusIndex = (this.focusIndex + 1) % this.focusPoints.length;
        const focus = this.focusPoints[this.focusIndex];
        this._moveToBoard({ x: 2 * focus.t + Number(focus.c), y: this._displayL(focus.l) }, this._focusScale());
    }

    addFocusPoint(l, t, c) {
        this.focusPoints.push({ l, t, c });
    }

    reloadColors() {
        this._loadColors();
        if (this.data) this._prepareBoardRasters(this.data.boards);
        this.filterCache.bounds = [];
        // force a redraw so new colors take effect
        this.infCanvas.startAnimation();
    }

    setFocusPoints(points) {
        this.focusPoints = points;
        this.focusIndex = 0;
    }
}

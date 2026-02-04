// draw.js
// chess board specific implementation

import { InfiniteScrollableCanvas } from 'canvas';

import { chooseLOD } from 'piece';
import { loadColors, applyFadeToColors, applyFadeToColorString, resolveColor } from './color.js';

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
        
        this.focusPoints = [{ l: 0, t: 0, c: 0 }];
        this.focusIndex = 0;
        
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
        this.infCanvas.onRightClick = (x, y) => this.handleBoardClick(x, y, true);
        this.infCanvas.onHover = (x, y) => this.updateStatus(x, y);
        
        this.infCanvas.startAnimation();
        this._loadColors();
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
        // Clear cache when data changes
        this.filterCache.bounds = [null, null, null, null];
        
        this.infCanvas.startAnimation();
    }

    // Filter board data based on visible bounds (with caching)
    _filterBoardData(lMin, lMax, vMin, vMax) {
        const cache = this.filterCache;
        const bounds = [lMin, lMax, vMin, vMax];
        
        // Check if cache is still valid
        if (cache.bounds.every((v, i) => v === bounds[i])) {
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
        if (this.data.highlights) {
            for (let colorBlock of this.data.highlights) {
                const color = this._resolveColor(colorBlock.color) || colorBlock.color;
                
                // Filter coordinate highlights
                if (colorBlock.coordinates) {
                    cache.filteredCoordinateHighlight[color] = colorBlock.coordinates.filter((pos) => {
                        let l = pos.l, v = pos.t << 1 | pos.c;
                        return !(l < lMin || l > lMax || v < vMin || v > vMax);
                    });
                }
                
                // Filter arrow highlights
                if (colorBlock.arrows) {
                    cache.filteredArrowHighlight[color] = colorBlock.arrows.filter((pos) => {
                        let l1 = pos.from.l, v1 = pos.from.t << 1 | pos.from.c;
                        let l2 = pos.to.l, v2 = pos.to.t << 1 | pos.to.c;
                        return !(l1 < lMin || l1 > lMax || v1 < vMin || v1 > vMax) ||
                               !(l2 < lMin || l2 > lMax || v2 < vMin || v2 > vMax);
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
        const l = pos.l, v = pos.t << 1 | pos.c;
        const shiftX = v * this.boardSkipX;
        const shiftY = l * this.boardSkipY;
        return [
            pos.x * this.squareSize + shiftX,
            (this.boardLengthY - 1 - pos.y) * this.squareSize + shiftY
        ];
    }

    // Main drawing method
    drawBoards(ctx, lMin, lMax, vMin, vMax) {
        if (!this.data) return;
        
        // Update camera status display
        if (this.cameraElement) {
            this.cameraElement.innerHTML = `(L${lMin}V${vMin}) -- (L${lMax}V${vMax})`;
        }
        
        // Filter data based on visible bounds (with caching)
        this._filterBoardData(lMin, lMax, vMin, vMax);
        
        const cache = this.filterCache;
        const squareLength = this.infCanvas.cameraCurrent.getZoomLevel() * this.squareSize;
        
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
                this.infCanvas.canvas.width, 
                this.infCanvas.canvas.height
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
            const lOpacity = smoothClamp(
                this.fontSizeforLT / 3.0,
                3 * this.fontSizeforLT,
                deltaX * zoomLevel
            );
            const tOpacity = smoothClamp(
                this.fontSizeforLT / 3.0,
                3 * this.fontSizeforLT,
                deltaY * zoomLevel
            );
            return {l, t, lOpacity, tOpacity};
        })();
        
        ctx.font = `${this.fontSizeforLT}px serif`;
        ctx.save();
        if(opacityValues.lOpacity > 0)
        {
            ctx.globalAlpha = opacityValues.lOpacity;
            ctx.fillStyle = this.colors.spGridWhite;
            for (let l = lMin - 1; l <= lMax; l++) {
                let t = opacityValues.t;
                if ((l + t) % 2 === 0) {
                    ctx.fillText(
                        `L${l}`,
                        t * 2 * this.boardSkipX - this.backgroundShiftX - 2,
                        l * this.boardSkipY - this.backgroundShiftY + this.fontSizeforLT
                    );
                }
            }
            ctx.fillStyle = this.colors.spGridBlack;
            for (let l = lMin - 1; l <= lMax; l++) {
                let t = opacityValues.t;
                if ((l + t) % 2 !== 0) {
                    ctx.fillText(
                        `L${l}`,
                        t * 2 * this.boardSkipX - this.backgroundShiftX - 2,
                        l * this.boardSkipY - this.backgroundShiftY + this.fontSizeforLT
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
        
        // Layer 2: Board margins
        for (const board of cache.filteredBoards) {
            let l = board.l, v = board.t << 1 | board.c;
            const shiftX = v * this.boardSkipX;
            const shiftY = l * this.boardSkipY;
            
            ctx.fillStyle = (board.c == 1) ? this.colors.boardMarginBlack : this.colors.boardMarginWhite;
            ctx.fillRect(
                shiftX - this.boardMargin,
                shiftY - this.boardMargin,
                this.boardLengthX * this.squareSize + 2 * this.boardMargin,
                this.boardLengthY * this.squareSize + 2 * this.boardMargin
            );
        }
        
        // Board highlights
        for (let color in cache.filteredBoardHighlight) {
            ctx.fillStyle = color;
            for (let pos of cache.filteredBoardHighlight[color]) {
                let l = pos.l, v = pos.t << 1 | pos.c;
                const shiftX = v * this.boardSkipX;
                const shiftY = l * this.boardSkipY;
                ctx.fillRect(
                    shiftX - this.boardMargin,
                    shiftY - this.boardMargin,
                    this.boardLengthX * this.squareSize + 2 * this.boardMargin,
                    this.boardLengthY * this.squareSize + 2 * this.boardMargin
                );
            }
        }
        
        
        if(squareLength > 0.7) {
            // Checkerboard pattern
            for (const board of cache.filteredBoards) {
                let l = board.l, v = board.t << 1 | board.c;
                const shiftX = v * this.boardSkipX;
                const shiftY = l * this.boardSkipY;
                
                ctx.fillStyle = this.colors.squareBlack;
                ctx.fillRect(shiftX, shiftY, this.boardLengthX * this.squareSize, this.boardLengthY * this.squareSize);
                
                ctx.fillStyle = this.colors.squareWhite;
                for (let row = 0; row < this.boardLengthY; row++) {
                    for (let col = 0; col < this.boardLengthX; col++) {
                        if ((row + col) % 2 === 0) {
                            ctx.fillRect(
                                col * this.squareSize + shiftX,
                                row * this.squareSize + shiftY,
                                this.squareSize,
                                this.squareSize
                            );
                        }
                    }
                }
            }
        } else {
            for (const board of cache.filteredBoards) {
                let l = board.l, v = board.t << 1 | board.c;
                const shiftX = v * this.boardSkipX;
                const shiftY = l * this.boardSkipY;
                
                ctx.fillStyle = this.colors.squareFuzzy;
                ctx.fillRect(shiftX, shiftY, this.boardLengthX * this.squareSize, this.boardLengthY * this.squareSize);
            }
        }
            
        // Layer 3: Highlighted squares
        ctx.save();
        //ctx.globalAlpha = 0.5;
        for (let color in cache.filteredCoordinateHighlight) {
            ctx.fillStyle = color;
            for (let pos of cache.filteredCoordinateHighlight[color]) {
                ctx.fillRect(...this._getCoordinate(pos), this.squareSize, this.squareSize);
            }
        }
        ctx.restore();
        
        if(squareLength > 0.7) {
            // Layer 4: Pieces
            const imgs = chooseLOD(squareLength);
            for (const board of cache.filteredBoards) {
                let l = board.l, v = board.t << 1 | board.c;
                const shiftX = v * this.boardSkipX;
                const shiftY = l * this.boardSkipY;
                
                let parsedBoard = board.parsed;
                for (let row = 0; row < this.boardLengthY; row++) {
                    for (let col = 0; col < this.boardLengthX; col++) {
                        const piece = parsedBoard[row][col];
                        if (isNaN(piece) && imgs[piece]) {
                            ctx.drawImage(
                                imgs[piece],
                                col * this.squareSize + shiftX,
                                row * this.squareSize + shiftY,
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
                ctx.save();
                
                const [fromX, fromY] = this._getCoordinate(arrow.from);
                const [toX, toY] = this._getCoordinate(arrow.to);
                const r = this.squareSize / 2;
                
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
        this.drawBoards(ctx, lMin, lMax, vMin, vMax);
        
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
        const l = Math.floor(worldY / this.boardSkipY);
        const v = Math.floor(worldX / this.boardSkipX);
        const c = (v & 1) !== 0;
        const t = v >> 1;
        const x = Math.floor((worldX - v * this.boardSkipX) / this.squareSize);
        const y = this.boardLengthY - 1 - Math.floor((worldY - l * this.boardSkipY) / this.squareSize);
        
        return { l, t, c, x, y };
    }

    handleBoardClick(worldX, worldY, isRightClick) {
        const pos = this.worldToBoard(worldX, worldY);

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

    // Override these methods in your implementation
    onClickSquare(l, t, c, x, y) {
        // Placeholder - override this method
        console.log('Left click:', { l, t, c, x, y });
        //report_click(l, t, c, x, y);
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

    goToNextFocus() {
        this.focusIndex = (this.focusIndex + 1) % this.focusPoints.length;
        if (this.focusPoints[this.focusIndex] !== undefined) {
            const focus = this.focusPoints[this.focusIndex];
            
            const actualScale = this.infCanvas.canvas.width / 120 / 3;
            const targetScale = Math.log2(actualScale);
            const targetX = this.boardSkipX * (focus.t << 1 | focus.c) + this.boardLengthX * this.squareSize / 2;
            const targetY = this.boardSkipY * focus.l + this.boardLengthY * this.squareSize / 2;
            
            this.infCanvas.moveTo(targetX, targetY, targetScale);
        }
    }

    addFocusPoint(l, t, c) {
        this.focusPoints.push({ l, t, c });
    }

    reloadColors() {
        this._loadColors();
        // force a redraw so new colors take effect
        this.infCanvas.startAnimation();
    }

    setFocusPoints(points) {
        this.focusPoints = points;
        this.focusIndex = 0;
    }
}

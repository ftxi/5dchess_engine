// draw.js

import { InfiniteScrollableCanvas } from 'canvas';
import { parse_FEN } from 'parse';
import { chooseLOD } from 'piece';

// ============================================================================
// CHESS BOARD SPECIFIC IMPLEMENTATION
// ============================================================================

export default class ChessBoardCanvas 
{
    constructor(canvasId, centerButtonId) {
        this.statusElement = document.getElementById('status');
        this.cameraElement = document.getElementById('camera');
        
        // Chess board configuration (actual values)
        this.boardLengthX = 8;
        this.boardLengthY = 8;
        this.squareSize = 10;
        this.boardMargin = 4;
        this.boardSkipX = 100;
        this.boardSkipY = 122;
        
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
        this.canvas = new InfiniteScrollableCanvas(canvasId, {
            minZoom: -5.0,
            maxZoom: 7.0,
            initialZoom: -1.0
        });
        
        // Set up callbacks
        this.canvas.onRender = (ctx, bounds) => this.renderBoards(ctx, bounds);
        this.canvas.onClick = (x, y, _e) => this.handleBoardClick(x, y, false);
        this.canvas.onRightClick = (x, y, _e) => this.handleBoardClick(x, y, true);
        this.canvas.onHover = (x, y) => this.updateStatus(x, y);
        
        // Center button
        if (centerButtonId) {
            document.getElementById(centerButtonId)?.addEventListener('click', 
                () => this.goToNextFocus());
        }
        
        this.canvas.startAnimation();
    }

    // Set the board data (replaces global data variable)
    setData(data) {
        this.data = data;
        
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
        if(data.focus)
        {
            this.focusPoints = data.focus;
        }
        // Clear cache when data changes
        this.filterCache.bounds = [null, null, null, null];
        
        this.canvas.startAnimation();
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
                const color = colorBlock.color;
                
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
        const backgroundShiftX = (this.boardSkipX - this.squareSize * this.boardLengthX) / 2;
        const backgroundShiftY = (this.boardSkipY - this.squareSize * this.boardLengthY) / 2;
        
        // Layer 1: Background grid on multiverse
        ctx.fillStyle = '#ffffff';
        ctx.fillRect(
            vMin * this.boardSkipX - backgroundShiftX,
            lMin * this.boardSkipY - backgroundShiftY,
            (vMax - vMin + 1) * this.boardSkipX,
            (lMax - lMin + 1) * this.boardSkipY
        );
        
        ctx.fillStyle = '#f5f5f5';
        for (let l = lMin - 1; l <= lMax; l++) {
            for (let t = vMin >> 1; t <= vMax >> 1; t++) {
                if ((l + t) % 2 === 0) {
                    ctx.fillRect(
                        t * 2 * this.boardSkipX - backgroundShiftX,
                        l * this.boardSkipY - backgroundShiftY,
                        2 * this.boardSkipX,
                        this.boardSkipY
                    );
                }
            }
        }
        
        // Layer 1.1: Highlighted timelines
        ctx.save();
        ctx.globalAlpha = 0.2;
        for (let color in cache.filteredTimelineHighlight) {
            ctx.fillStyle = color;
            for (let l of cache.filteredTimelineHighlight[color]) {
                ctx.fillRect(
                    vMin * this.boardSkipX - backgroundShiftX,
                    l * this.boardSkipY - backgroundShiftY,
                    (vMax - vMin + 1) * this.boardSkipX,
                    this.boardSkipY
                );
            }
        }
        ctx.restore();
        
        // Layer 1.2: Present column
        if (this.data.present) {
            let t = this.data.present.t, c = this.data.present.c;
            let color = this.data.present.color || 'rgba(219,172,52,0.4)';
            ctx.fillStyle = color;
            ctx.fillRect(
                (t * 2 + c) * this.boardSkipX - backgroundShiftX,
                lMin * this.boardSkipY - backgroundShiftY,
                this.boardSkipX,
                (lMax - lMin + 1) * this.boardSkipY
            );
        }
        
        // Layer 2: Board margins
        for (const board of cache.filteredBoards) {
            let l = board.l, v = board.t << 1 | board.c;
            const shiftX = v * this.boardSkipX;
            const shiftY = l * this.boardSkipY;
            
            ctx.fillStyle = (board.c == 1) ? '#555555' : '#dfdfdf';
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
        
        const squareLength = this.canvas.cameraCurrent.getZoomLevel() * this.squareSize;
        if(squareLength > 0.7) {
            // Checkerboard pattern
            for (const board of cache.filteredBoards) {
                let l = board.l, v = board.t << 1 | board.c;
                const shiftX = v * this.boardSkipX;
                const shiftY = l * this.boardSkipY;
                
                ctx.fillStyle = '#7f7f7f';
                ctx.fillRect(shiftX, shiftY, this.boardLengthX * this.squareSize, this.boardLengthY * this.squareSize);
                
                ctx.fillStyle = '#cccccc';
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
                
                ctx.fillStyle = '#a6a6a6';
                ctx.fillRect(shiftX, shiftY, this.boardLengthX * this.squareSize, this.boardLengthY * this.squareSize);
            }
        }
            
        // Layer 3: Highlighted squares
        ctx.save();
        ctx.globalAlpha = 0.5;
        for (let color in cache.filteredCoordinateHighlight) {
            ctx.fillStyle = color;
            for (let pos of cache.filteredCoordinateHighlight[color]) {
                ctx.fillRect(...this._getCoordinate(pos), this.squareSize, this.squareSize);
            }
        }
        ctx.restore();
        
        if(squareLength > 0.7) {
            // Layer 4: Pieces
            this.cameraElement.innerText = `square length: ${squareLength}`;
            const imgs = chooseLOD(squareLength);
            for (const board of cache.filteredBoards) {
                let l = board.l, v = board.t << 1 | board.c;
                const shiftX = v * this.boardSkipX;
                const shiftY = l * this.boardSkipY;
                
                let parsedBoard = parse_FEN(board.fen);
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
                gradient.addColorStop(0, "rgba(255,255,255,0.0)");
                gradient.addColorStop(u / 3, "rgba(255,255,255,0.0)");
                gradient.addColorStop(u, "rgba(255,255,255,0.8)");
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
        
        // Debug: origin indicator
        ctx.fillStyle = 'red';
        ctx.fillRect(0, 0, 10, 10);
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
            
            const actualScale = this.canvas.canvas.width / 120 / 3;
            const targetScale = Math.log2(actualScale);
            const targetX = this.boardSkipX * (focus.t << 1 | focus.c) + this.boardLengthX * this.squareSize / 2;
            const targetY = this.boardSkipY * focus.l + this.boardLengthY * this.squareSize / 2;
            
            this.canvas.moveTo(targetX, targetY, targetScale);
        }
    }

    addFocusPoint(l, t, c) {
        this.focusPoints.push({ l, t, c });
    }

    setFocusPoints(points) {
        this.focusPoints = points;
        this.focusIndex = 0;
    }
}

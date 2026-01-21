
// ============================================================================
// GENERIC INFINITE SCROLLABLE CANVAS
// ============================================================================

export class AnimationManager {
    constructor(callback) {
        this.running = false;
        this.timeRecord = 0;
        this.callback = callback;
    }

    start() {
        if (this.running) return;
        this.running = true;
        this.timeRecord = 0;
        requestAnimationFrame((time) => this._animate(time));
    }

    stop() {
        this.running = false;
    }

    _animate(timeNow) {
        let timeDiff = 20;
        if (timeNow && this.timeRecord) {
            timeDiff = Math.min(timeNow - this.timeRecord, 100);
        }
        this.timeRecord = timeNow;

        const shouldContinue = this.callback(timeDiff);
        
        if (this.running && shouldContinue) {
            requestAnimationFrame((time) => this._animate(time));
        } else {
            this.running = false;
        }
    }
}

class Camera {
    constructor(x = 0, y = 0, scale = 0) {
        this.x = x;
        this.y = y;
        this.scale = scale; // logarithmic scale (base 2)
    }

    clone() {
        return new Camera(this.x, this.y, this.scale);
    }

    isCloseTo(other, positionThreshold = 0.1, scaleThreshold = 0.02) {
        return Math.abs(this.x - other.x) < positionThreshold 
            && Math.abs(this.y - other.y) < positionThreshold 
            && Math.abs(this.scale / other.scale - 1) < scaleThreshold;
    }

    lerpTowards(target, timeDelta, speed = 0.01) {
        if (this.isCloseTo(target)) return;
        
        const lerpFactor = 1.0 - 1.0 / (1.0 + timeDelta * speed);
        this.x += (target.x - this.x) * lerpFactor;
        this.y += (target.y - this.y) * lerpFactor;
        this.scale += (target.scale - this.scale) * lerpFactor;
    }

    getZoomLevel() {
        return Math.pow(2, this.scale);
    }

    screenToWorld(screenX, screenY, canvasWidth, canvasHeight) {
        const centerX = canvasWidth / 2;
        const centerY = canvasHeight / 2;
        const zoomLevel = this.getZoomLevel();
        
        return {
            x: (screenX - centerX) / zoomLevel - this.x,
            y: (screenY - centerY) / zoomLevel - this.y
        };
    }

    worldToScreen(worldX, worldY, canvasWidth, canvasHeight) {
        const centerX = canvasWidth / 2;
        const centerY = canvasHeight / 2;
        const zoomLevel = this.getZoomLevel();
        
        return {
            x: (worldX + this.x) * zoomLevel + centerX,
            y: (worldY + this.y) * zoomLevel + centerY
        };
    }

    getVisibleWorldBounds(canvasWidth, canvasHeight) {
        const topLeft = this.screenToWorld(0, 0, canvasWidth, canvasHeight);
        const bottomRight = this.screenToWorld(canvasWidth, canvasHeight, canvasWidth, canvasHeight);
        
        return {
            left: topLeft.x,
            top: topLeft.y,
            right: bottomRight.x,
            bottom: bottomRight.y,
            width: bottomRight.x - topLeft.x,
            height: bottomRight.y - topLeft.y
        };
    }
}

export class InfiniteScrollableCanvas {
    constructor(canvasId, options = {}) {
        this.canvas = document.getElementById(canvasId);
        if (!this.canvas) {
            throw new Error(`Canvas element with id "${canvasId}" not found`);
        }
        
        this.ctx = this.canvas.getContext('2d');
        
        // Configuration
        this.config = {
            minZoom: options.minZoom ?? -5.0,
            maxZoom: options.maxZoom ?? 7.0,
            zoomSpeed: options.zoomSpeed ?? 0.005,
            dragSpeed: options.dragSpeed ?? 1.0,
            lerpSpeed: options.lerpSpeed ?? 0.01,
            ...options
        };
        
        // Camera state
        this.cameraTarget = new Camera(
            options.initialX ?? 0,
            options.initialY ?? 0,
            options.initialZoom ?? -1.0
        );
        this.cameraCurrent = this.cameraTarget.clone();
        
        // Input state
        this.isMouseDown = false;
        this.isDragging = false;
        this.dragStart = { x: 0, y: 0 };
        this.lastMousePos = { x: 0, y: 0 };
        
        // Animation
        this.animationManager = new AnimationManager((timeDiff) => this._onAnimationFrame(timeDiff));
        
        // Callbacks (to be overridden by implementation)
        this.onRender = null;
        this.onClick = null;
        this.onRightClick = null;
        this.onHover = null;
        
        this._setupEventListeners();
        this.resize();
    }

    _setupEventListeners() {
        // Mouse events
        this.canvas.addEventListener('mousedown', (e) => this._handleMouseDown(e));
        this.canvas.addEventListener('mouseup', (e) => this._handleMouseUp(e));
        this.canvas.addEventListener('mousemove', (e) => this._handleMouseMove(e));
        this.canvas.addEventListener('mouseover', () => this.isMouseDown = false);
        this.canvas.addEventListener('mouseout', () => this.isMouseDown = false);
        this.canvas.addEventListener('wheel', (e) => this._handleWheel(e));
        this.canvas.addEventListener('contextmenu', (e) => e.preventDefault());
        
        // Window resize
        window.addEventListener('resize', () => this.resize());
    }

    _handleMouseDown(e) {
        if (e.button !== 0) return; // Only left mouse button for dragging
        
        this.isMouseDown = true;
        const dragSpeed = this.config.dragSpeed / this.cameraCurrent.getZoomLevel();
        this.dragStart.x = dragSpeed * e.clientX - this.cameraTarget.x;
        this.dragStart.y = dragSpeed * e.clientY - this.cameraTarget.y;
        this.isDragging = false;
    }

    _handleMouseUp(e) {
        this.isMouseDown = false;
        //console.log(`_handleMouseUp ${e.button} ${this.isDragging}`);

        if (e.button === 0 && this.isDragging) {
            return; // Don't treat drag release as click
        }
        
        const worldPos = this._getMouseWorldPosition(e);

        if (e.button === 0 && this.onClick) {
            this.onClick(worldPos.x, worldPos.y, e);
        } else if (e.button === 2 && this.onRightClick) {
            this.onRightClick(worldPos.x, worldPos.y, e);
        }
    }

    _handleMouseMove(e) {
        const worldPos = this._getMouseWorldPosition(e);
        this.lastMousePos = worldPos;
        
        if (this.isMouseDown) {
            const dragSpeed = this.config.dragSpeed / this.cameraCurrent.getZoomLevel();
            this.cameraTarget.x = dragSpeed * e.clientX - this.dragStart.x;
            this.cameraTarget.y = dragSpeed * e.clientY - this.dragStart.y;
            this.startAnimation();
        }
        
        if (this.onHover) {
            this.onHover(worldPos.x, worldPos.y, e);
        }
        this.isDragging = true;
    }

    _handleWheel(e) {
        e.preventDefault();
        
        this.cameraTarget.scale += e.deltaY * -this.config.zoomSpeed;
        this.cameraTarget.scale = Math.max(this.config.minZoom, 
                                           Math.min(this.config.maxZoom, this.cameraTarget.scale));
        this.startAnimation();
    }

    _getMouseWorldPosition(e) {
        const rect = this.canvas.getBoundingClientRect();
        const screenX = (e.clientX - rect.left) / (rect.right - rect.left) * this.canvas.width;
        const screenY = (e.clientY - rect.top) / (rect.bottom - rect.top) * this.canvas.height;
        
        return this.cameraCurrent.screenToWorld(screenX, screenY, this.canvas.width, this.canvas.height);
    }

    _onAnimationFrame(timeDiff) {
        // Update camera position
        this.cameraCurrent.lerpTowards(this.cameraTarget, timeDiff, this.config.lerpSpeed);
        
        // Render
        this._render();
        
        // Continue animating if camera hasn't reached target
        return !this.cameraCurrent.isCloseTo(this.cameraTarget);
    }

    _render() {
        this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
        this.ctx.save();
        
        // Apply camera transform
        const centerX = this.canvas.width / 2;
        const centerY = this.canvas.height / 2;
        const zoom = this.cameraCurrent.getZoomLevel();
        
        this.ctx.translate(centerX, centerY);
        this.ctx.scale(zoom, zoom);
        this.ctx.translate(this.cameraCurrent.x, this.cameraCurrent.y);
        
        // Call user-defined render callback
        if (this.onRender) {
            const bounds = this.cameraCurrent.getVisibleWorldBounds(
                this.canvas.width, 
                this.canvas.height
            );
            this.onRender(this.ctx, bounds);
        }
        
        this.ctx.restore();
    }

    startAnimation() {
        this.animationManager.start();
    }

    resize() {
        this.canvas.width = window.innerWidth;
        this.canvas.height = window.innerHeight;
        this._render();
    }

    moveTo(x, y, zoom = null) {
        this.cameraTarget.x = -x;
        this.cameraTarget.y = -y;
        if (zoom !== null) {
            this.cameraTarget.scale = zoom;
        }
        this.startAnimation();
    }

    resetView() {
        this.moveTo(0, 0, this.config.initialZoom ?? -1.0);
    }

    getCamera() {
        return this.cameraCurrent.clone();
    }

    getVisibleBounds() {
        return this.cameraCurrent.getVisibleWorldBounds(
            this.canvas.width,
            this.canvas.height
        );
    }
}


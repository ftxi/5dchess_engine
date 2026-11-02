import { reportLoaded } from './startup.js';
const LOD_SIZES = [32, 16, 8, 4, 2, 1];

// Load a single SVG as Image
function loadSvgImage(filename) {
    return new Promise((resolve, reject) => {
        const img = new Image();
        img.src = filename;
        img.onload = () => resolve(img);
        img.onerror = () => reject(new Error(`Failed to load: ${filename}`));
    });
}

// Shared canvas allocation and raster pyramid generation for pieces and boards.
function createSurface(width, height) {
    const canvas = document.createElement('canvas');
    canvas.width = Math.max(1, Math.ceil(width));
    canvas.height = Math.max(1, Math.ceil(height));
    return canvas;
}

function createLevels(width, height, unit, draw) {
    const levels = {};
    let previous;
    for (const size of LOD_SIZES) {
        const canvas = createSurface(width * size / unit, height * size / unit);
        const ctx = canvas.getContext('2d');
        if (previous) ctx.drawImage(previous, 0, 0, canvas.width, canvas.height);
        else draw(ctx, canvas.width, canvas.height);
        levels[size] = canvas;
        previous = canvas;
    }
    return levels;
}

function levelSize(pixelSize) {
    if (pixelSize >= 24) return null;
    if (pixelSize >= 12) return 32;
    if (pixelSize >= 6) return 16;
    if (pixelSize >= 3) return 8;
    if (pixelSize >= 2) return 4;
    if (pixelSize >= 1) return 2;
    return 1;
}

// Cache complete pyramids, with opacity applied by the caller at composition.
export class SurfaceLOD {
    constructor() {
        this.entries = new Map();
        this.pixels = 0;
    }

    prepare(key, width, height, unit, draw) {
        if (this.entries.has(key)) return;
        const levels = createLevels(width, height, unit, draw);
        const pixels = Object.values(levels).reduce((sum, image) => sum + image.width * image.height, 0);
        const budget = 2 * 1024 * 1024;
        if (pixels > budget) return;
        while (this.pixels + pixels > budget && this.entries.size) {
            const oldest = this.entries.keys().next().value;
            this.pixels -= this.entries.get(oldest).pixels;
            this.entries.delete(oldest);
        }
        this.entries.set(key, { levels, pixels });
        this.pixels += pixels;
    }

    get(key, pixelSize) {
        const size = levelSize(pixelSize);
        const entry = this.entries.get(key);
        if (!entry || size === null) return null;
        this.entries.delete(key);
        this.entries.set(key, entry);
        return entry.levels[size];
    }
}

// One display-resolution scratch surface is reused between translucent boards.
export function composeSurface(ctx, width, height, pixelScale, x, y, opacity, draw, bounds) {
    const offsetX = bounds ? Math.max(0, bounds.left - x) : 0;
    const offsetY = bounds ? Math.max(0, bounds.top - y) : 0;
    const clippedWidth = bounds ? Math.min(width, bounds.right - x) - offsetX : width;
    const clippedHeight = bounds ? Math.min(height, bounds.bottom - y) - offsetY : height;
    if (clippedWidth <= 0 || clippedHeight <= 0) return;
    if (!scratch) scratch = createSurface(1, 1);
    const pixelWidth = Math.max(1, Math.ceil(clippedWidth * pixelScale));
    const pixelHeight = Math.max(1, Math.ceil(clippedHeight * pixelScale));
    if (scratch.width !== pixelWidth) scratch.width = pixelWidth;
    if (scratch.height !== pixelHeight) scratch.height = pixelHeight;
    const surface = scratch.getContext('2d');
    surface.setTransform(1, 0, 0, 1, 0, 0);
    surface.clearRect(0, 0, pixelWidth, pixelHeight);
    surface.save();
    surface.scale(pixelWidth / clippedWidth, pixelHeight / clippedHeight);
    surface.translate(-offsetX, -offsetY);
    draw(surface);
    surface.restore();
    ctx.save();
    ctx.globalAlpha *= opacity;
    ctx.drawImage(scratch, x + offsetX, y + offsetY, clippedWidth, clippedHeight);
    ctx.restore();
}
let scratch;

async function loadAllSvgs() {
    const svgImages = {
        img: {}, // original SVGs
        lod: {}  // { size: { name: canvas, ... } }
    };

    // Initialize lod object for each size
    for (const size of LOD_SIZES) {
        svgImages.lod[size] = {};
    }

    // Load all SVGs
    const tasks = Object.entries(window.svg_file_mapping).map(
        async ([name, filename]) => {
            const img = await loadSvgImage(filename);
            svgImages.img[name] = img;

            const levels = createLevels(1, 1, 1,
                (ctx, width, height) => ctx.drawImage(img, 0, 0, width, height));
            for (const size of LOD_SIZES) svgImages.lod[size][name] = levels[size];
        }
    );

    await Promise.all(tasks);

    // console.log("All SVGs + LODs loaded:", svgImages);
    return svgImages;
}

const svgImages = await loadAllSvgs();
reportLoaded('pieces');

export function chooseLOD(pixelSize) {
    const size = levelSize(pixelSize);
    return size === null ? svgImages.img : svgImages.lod[size];
}

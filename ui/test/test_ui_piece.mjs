import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

const source = (await readFile(new URL('../scripts/level-of-detail.js', import.meta.url), 'utf8'))
    .replace("import { reportLoaded } from './startup.js';", 'function reportLoaded() {}');

test('piece LOD caches remain independent and are reused across zoom changes', async () => {
    const originals = new Map(['window', 'document', 'Image', 'createImageBitmap'].map(key =>
        [key, Object.getOwnPropertyDescriptor(globalThis, key)]));
    let rasterizations = 0;
    try {
        globalThis.window = { svg_file_mapping: { p: 'pawn-black.svg', P: 'pawn-white.svg' } };
        globalThis.Image = class {
            set src(value) {
                this.filename = value;
                queueMicrotask(() => this.onload());
            }
        };
        globalThis.createImageBitmap = () => { throw new Error('LOD must not require ImageBitmap'); };
        globalThis.document = {
            createElement(tag) {
                assert.equal(tag, 'canvas');
                const canvas = { width: 0, height: 0 };
                canvas.getContext = type => {
                    assert.equal(type, '2d');
                    return {
                        drawImage(image, x, y, width, height) {
                            rasterizations++;
                            canvas.pixels = { filename: image.filename ?? image.pixels.filename, x, y, width, height };
                        }
                    };
                };
                return canvas;
            }
        };
        const { chooseLOD } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
        const originalImages = chooseLOD(24);
        const levels = [[12, 32], [6, 16], [3, 8], [2, 4], [1, 2], [0.5, 1]];
        const cached = new Set();
        for (const [pixelSize, size] of levels) {
            const images = chooseLOD(pixelSize);
            for (const [name, filename] of Object.entries(window.svg_file_mapping)) {
                const image = images[name];
                assert.ok(!cached.has(image), 'pieces and sizes must not share a mutable cache');
                cached.add(image);
                assert.equal(image.width, size);
                assert.equal(image.height, size);
                assert.deepEqual(image.pixels, { filename, x: 0, y: 0, width: size, height: size });
            }
            assert.equal(chooseLOD(pixelSize), images, 'zooming reuses the loaded cache');
        }
        assert.equal(chooseLOD(8 * 3), originalImages);
        assert.equal(chooseLOD(7.99 * 3), chooseLOD(12));
        assert.equal(rasterizations, cached.size, 'zooming must not rasterize the pieces again');
    } finally {
        for (const [key, descriptor] of originals) {
            if (descriptor) Object.defineProperty(globalThis, key, descriptor);
            else delete globalThis[key];
        }
    }
});

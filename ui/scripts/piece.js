
const LOD_SIZES = [64, 32, 16, 8, 4, 2];

// Load a single SVG as Image
function loadSvgImage(filename) {
    return new Promise((resolve, reject) => {
        const img = new Image();
        img.src = filename;
        img.onload = () => resolve(img);
        img.onerror = () => reject(new Error(`Failed to load: ${filename}`));
    });
}

const tempCanvas = document.createElement("canvas");
const tempCtx = tempCanvas.getContext("2d");

// Rasterize Image to given size (returns ImageBitmap)
async function rasterizeImage(img, size) {
    tempCanvas.width = size;
    tempCanvas.height = size;
    tempCtx.clearRect(0, 0, size, size);
    tempCtx.drawImage(img, 0, 0, size, size);

    return await createImageBitmap(tempCanvas);
}

async function loadAllSvgs() {
    const svgImages = {
        img: {}, // original SVGs
        lod: {}  // { size: { name: bitmap, ... } }
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

            // Generate LODs
            for (const size of LOD_SIZES) {
                const bitmap = await rasterizeImage(img, size);
                svgImages.lod[size][name] = bitmap;
            }
        }
    );

    await Promise.all(tasks);

    // console.log("All SVGs + LODs loaded:", svgImages);
    return svgImages;
}

const svgImages = await loadAllSvgs();

export function chooseLOD(pixelSize) {
    if (pixelSize >= 48) return svgImages.img;
    if (pixelSize >= 24) return svgImages.lod[64];
    if (pixelSize >= 12) return svgImages.lod[32];
    if (pixelSize >= 6)  return svgImages.lod[16];
    if (pixelSize >= 3)  return svgImages.lod[8];
    if (pixelSize >= 2)  return svgImages.lod[4];
    return svgImages.lod[2];
}

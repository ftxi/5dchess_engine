
function hashStringList(list) {
    let hash = 2046; // Seed
    for (let i = 0; i < list.length; i++) {
    const str = list[i];
    for (let j = 0; j < str.length; j++) {
            // hash * 33 + charCode
            hash = ((hash << 5) + hash) + str.charCodeAt(j);
            hash |= 0; // Force to 32-bit integer
        }
    }
    return (hash ^ (hash >>> 16)) & 0xFFFF; // Finalize to 16-bit
}

function hashAction(action) {
    let hash = 1818; // Seed
    for (let move of action) {
        let a = move.from.x | move.from.y << 4 | move.from.l << 8 | move.from.t << 16;
        let b = move.to.x | move.to.y << 4 | move.to.l << 8 | move.to.t << 16;
        hash = ((hash << 5) + hash) + a;
        hash = ((hash << 5) + hash) + b;
        hash |= 0; // Force to 32-bit integer
    }
    return hash >>> 0;
}

function hashActionList(actionList) {
    let hash = 2046; // Seed
    for (let action of actionList) {
        hash = ((hash << 5) + hash) ^ hashAction(action);
        hash |= 0; // Force to 32-bit integer
    }
    return (hash ^ (hash >>> 16)) & 0xFFFF; // Finalize to 16-bit
}

export { hashStringList, hashAction, hashActionList };

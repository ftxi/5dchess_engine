// test.js
/* This file, together with the `package.json` file in the same folder
are **not** a part of UI. Instead, they are a standalone test script. */
import createModule from './engine.js';

async function runTest() {
  try {
    const engine_wasm = await createModule();
    console.log('Module loaded!');

    const g0 = engine_wasm.from_pgn('[Board "Standard"]');
    if (!g0.success) {
      console.error('Failed to load game:', g0.message);
      return;
    }

    const g = g0.game;
    console.log('Initial game object:', g);

    const mvs = g.gen_move_if_playable({ l: 0, t: 1, x: 1, y: 0, c: true });
    console.log(
      `moves: ${mvs.length}, first move: l=${mvs[0].l}, t=${mvs[0].t}, x=${mvs[0].x}, y=${mvs[0].y}`
    );
    console.log(`mvs: ${JSON.stringify(mvs)}`);

    g.suggest_action();
    g.suggest_action();
    g.suggest_action();

    const suggestions = g.get_child_moves();
    console.log('Suggested actions:', suggestions);

    // Visit the first suggested move
    g.visit_child(suggestions[0]);

    // Show PGN and boards
    console.log('PGN after suggestion:', g.show_pgn());
    console.log('Current boards:', g.get_current_boards());
  } catch (err) {
    console.error('Error loading module:', err);
  }
}

// Run the test
runTest();

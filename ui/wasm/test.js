// test.js
/* This file, together with the `package.json` file in the same folder
are **not** a part of UI. Instead, they are a standalone test script. */
import createModule from './engine.js';
import { readFile } from 'node:fs/promises';
const { parse_FEN } = await import('data:text/javascript,' + encodeURIComponent(
    await readFile(new URL('../scripts/parse.js', import.meta.url), 'utf8')));

async function runTest() {
  try {
    const engine_wasm = await createModule();
    console.log('Module loaded!');

    const illegalSetup = `[Board "Custom"]
[Size "8x8"]
[Timeline "odd"]
[7k/8/8/8/8/8/8/K7:0:1:w]
[7k/3r4/8/8/3K4/8/8/8:1:1:b]`;
    if (engine_wasm.from_pgn(illegalSetup, { allowIllegalHistory: false }).success) throw new Error('Strict loading accepted illegal setup');
    const setupFailure = engine_wasm.from_pgn(illegalSetup, { allowIllegalHistory: false }).diagnostic;
    if (setupFailure.code !== 'royal_exposed' || !setupFailure.canAllowIllegalHistory
        || illegalSetup.slice(setupFailure.range.start, setupFailure.range.end) !== '[7k/3r4/8/8/3K4/8/8/8:1:1:b]') {
      throw new Error('Illegal setup diagnostic lost its board range or permission hint');
    }
    const unicode = '[Event "😀 café"]\n[Board "Standard - Turn Zero"]\n1. e2e5';
    const moveFailure = engine_wasm.from_pgn(unicode).diagnostic;
    if (moveFailure.code !== 'invalid_move' || unicode.slice(moveFailure.range.start, moveFailure.range.end) !== 'e2e5'
        || moveFailure.canAllowIllegalHistory) throw new Error('Validation range is not UTF-16');
    const syntaxInput = '[Event "😀"]\n[Board "Standard - Turn Zero"]\n1. e4 {';
    const syntaxFailure = engine_wasm.from_pgn(syntaxInput).diagnostic;
    if (syntaxFailure.category !== 'syntax' || syntaxFailure.code !== 'unterminated_comment'
        || syntaxInput.slice(syntaxFailure.range.start, syntaxFailure.range.end) !== '{') {
      throw new Error('Syntax diagnostic lost its Unicode source range');
    }
    const boardSyntax = '[Event "😀"]\n[7k/8/8/8/8/8/8/K7:0:1:q]';
    const boardFailure = engine_wasm.from_pgn(boardSyntax).diagnostic;
    if (boardFailure.category !== 'syntax' || boardSyntax.slice(boardFailure.range.start, boardFailure.range.end) !== 'q') {
      throw new Error('Board metadata syntax error was not rebased into the full PGN');
    }
    const eofInput = '[Event "😀"]\n[Board "Standard - Turn Zero"]\n1. e4 /';
    const eofFailure = engine_wasm.from_pgn(eofInput).diagnostic;
    if (eofFailure.range.start !== eofInput.length || eofFailure.range.end !== eofInput.length) {
      throw new Error('Missing-input diagnostic did not retain its empty UTF-16 EOF range');
    }
    for (const [header, value, code] of [
      ['BoArD', 'Unknown variant', 'unknown_variant'],
      ['Variant', '', 'unknown_variant'],
      ['Size', 'bad', 'invalid_size'],
      ['Size', 'ax8', 'invalid_size'],
      ['Size', '99999999999999999999x8', 'invalid_size'],
      ['Timeline', 'sideways', 'invalid_timeline'],
      ['Promotions', '*Q', 'invalid_promotions'],
    ]) {
      const input = `[Event "😀 café"]\n[${header} "${value}"]`;
      const diagnostic = engine_wasm.from_pgn(input, { allowIllegalHistory: true }).diagnostic;
      const start = input.lastIndexOf('"', input.length - 3) + 1;
      if (diagnostic.category !== 'validation' || diagnostic.code !== code
          || diagnostic.range.start !== start || diagnostic.range.end !== start + value.length
          || diagnostic.canAllowIllegalHistory) {
        throw new Error(`Incorrect header diagnostic: ${JSON.stringify(diagnostic)}`);
      }
    }
    const missingSetup = engine_wasm.from_pgn('[Event "No setup"]').diagnostic;
    if (missingSetup.code !== 'missing_setup' || missingSetup.range !== null || missingSetup.canAllowIllegalHistory) {
      throw new Error('Missing setup diagnostic invented a header range or permission');
    }
    const permissive = engine_wasm.from_pgn(illegalSetup, { allowIllegalHistory: true });
    if (!permissive.success) throw new Error(permissive.message);
    if (!permissive.game.has_illegal_history() || permissive.game.suggest_action() !== null
        || permissive.game.get_match_status() !== "White's Move (Illegal History)") {
      throw new Error('Illegal root status or Hint is incorrect');
    }
    permissive.game.delete();
    const policy = engine_wasm.from_pgn('[Board "Standard - Turn Zero"] 1. f3 / e5 2. g4 / Qh4').game;
    if (policy.can_submit() || policy.can_submit(true) || policy.submit(true)) {
      throw new Error('Incomplete turns must be rejected in both policies');
    }
    if (!policy.apply_move({ from: { x: 0, y: 1, t: 3, l: 0 }, to: { x: 0, y: 2, t: 3, l: 0 } })) {
      throw new Error('Could not prepare exposed draft');
    }
    if (policy.can_submit() || policy.submit() || !policy.can_submit(true) || policy.has_illegal_history()) {
      throw new Error('Default submission must reject exposed drafts without marking history');
    }
    if (!policy.submit(true) || !policy.has_illegal_history()) {
      throw new Error('Explicit permissive submission must record illegal history');
    }
    policy.delete();
    const history = engine_wasm.from_pgn('[Board "Standard - Turn Zero"] 1. f3 / e5 2. g4 / Qh4 3. a3 / a6', { allowIllegalHistory: true });
    if (!history.success) throw new Error(history.message);
    history.game.visit_parent();
    const inherited = history.game.get_child_actions()[0];
    if (!inherited.is_illegal || !Array.isArray(inherited.action) || typeof inherited.pgn !== 'string') {
      throw new Error('Child summary failed to expose inherited illegal history');
    }
    history.game.visit_parent();
    const unsafe = history.game.get_child_actions()[0];
    if (history.game.has_illegal_history() || !unsafe.is_illegal) throw new Error('Unsafe continuation was not flagged');
    if (!history.game.visit_child(unsafe.action)) throw new Error('Summary action is not navigable');
    history.game.delete();
    console.log('Illegal-history loading, status, Hint and summaries passed');

    const dragonPgn = `[Mode "5D"]
[Result "0-1"]
[Date "2023.05.13"]
[Time "19:08:21 (+02:00)"]
[Size "8x8"]
[White "Narik"]
[Black "Mauer01"]
[Event "Custom Variant tourney match 2"]
[Game "1"]
[Board "custom"]
[r*dbqk*bdr*/p*p*p*p*p*p*p*p*/1dDDdD1/222/222/1DddDd1/P*P*P*P*P*P*P*P*/R*DBQK*BDR*:0:1:w]`;
    const invalid = engine_wasm.from_pgn(dragonPgn);
    if (invalid.success || invalid.type !== 'invalid_argument'
        || invalid.message !== 'FEN rank has 7 squares; expected 8.') {
      throw new Error('An incomplete rank must produce a FEN error');
    }
    if (invalid.diagnostic.code !== 'rank_too_short'
        || dragonPgn.slice(invalid.diagnostic.range.start, invalid.diagnostic.range.end) !== '1dDDdD1') {
      throw new Error('FEN diagnostic lost its enclosing PGN range');
    }
    const walls = engine_wasm.from_pgn(dragonPgn.replace('1dDDdD1/222/222/1DddDd1',
      '1dDDdD1#/6##/6##/1DddDd1#'));
    if (!walls.success) throw new Error(walls.message);
    const wallBoard = walls.game.get_current_boards()[0];
    if ('parsed' in wallBoard) throw new Error('Board display must be reconstructed from FEN');
    const cells = parse_FEN(wallBoard.fen, 8, 8);
    if (cells.length !== 8 || cells.some(row => row.length !== 8)
        || cells.flat().filter(cell => cell === '#').length !== 6
        || cells[2][7] !== '#' || cells[3][6] !== '#' || cells[4][6] !== '#'
        || cells[5][7] !== '#' || cells[2][5] !== 'D') {
      throw new Error('Wall display cells lost their positions');
    }
    const reloadedWalls = engine_wasm.from_pgn(walls.game.show_pgn());
    if (!reloadedWalls.success) throw new Error(reloadedWalls.message);
    if (reloadedWalls.game.get_current_boards()[0].fen !== wallBoard.fen) {
      throw new Error('Wall FEN failed to round trip through PGN');
    }
    reloadedWalls.game.delete();
    walls.game.delete();

    const rectangular = engine_wasm.from_pgn('[Board "Custom"] [Size "4x3"] [k3/4/3K:0:1:w]');
    if (!rectangular.success) throw new Error(rectangular.message);
    const rectangle = parse_FEN(rectangular.game.get_current_boards()[0].fen, 4, 3);
    if (rectangle.length !== 3 || rectangle.some(row => row.length !== 4)
        || rectangle.flat().includes('#') || rectangle[0][0] !== 'k' || rectangle[2][3] !== 'K') {
      throw new Error('Display cells include padding outside the declared board');
    }
    rectangular.game.delete();
    console.log('Wall and rectangular board display tests passed');

    const g0 = engine_wasm.from_pgn('[Board "Standard"]');
    if (!g0.success) {
      console.error('Failed to load game:', g0.message);
      return;
    }

    const g = g0.game;
    console.log('Initial game object:', g);

    // Metadata API test: get_metadata/set_metadata roundtrip
    const initialMetadata = g.get_metadata();
    console.log('Initial metadata:', initialMetadata);

    const nextMetadata = {
      Event: 'WASM Binding Test',
      Site: 'Local',
      Date: '2026.03.18',
      Round: '1',
      White: 'TesterA',
      Black: 'TesterB',
      Result: '*',
      Variant: '5D Chess'
    };
    g.set_metadata(nextMetadata);

    const roundtripMetadata = g.get_metadata();
    console.log('Roundtrip metadata:', roundtripMetadata);

    for (const [k, v] of Object.entries(nextMetadata)) {
      if (roundtripMetadata[k] !== v) {
        throw new Error(`Metadata mismatch for key ${k}: expected ${v}, got ${roundtripMetadata[k]}`);
      }
    }
    console.log('Metadata roundtrip test passed');

    const duplicate = engine_wasm.from_pgn(`[Variant "Standard - Turn Zero"]
(1. Nf3 {old} / Nf6 2. Nc3 {discarded})
1. Ng1f3 {replacement}`);
    if (!duplicate.success) throw new Error(duplicate.message);
    const loaded = duplicate.game;
    if (loaded.get_comments()[0] !== 'replacement' || loaded.get_historical_actions().length !== 1
        || loaded.get_child_actions().length !== 0) throw new Error('Duplicate replacement endpoint is incorrect');
    loaded.visit_parent();
    const surviving = loaded.get_child_actions();
    if (surviving.length !== 1 || !loaded.has_child(surviving[0].action)
        || loaded.add_child(surviving[0].action)) throw new Error('Duplicate membership is incorrect');
    loaded.delete();
    console.log('Duplicate PGN replacement test passed');

    const mvs = g.gen_move_if_playable({ l: 0, t: 1, x: 1, y: 0, c: true });
    console.log(
      `moves: ${mvs.length}, first move: l=${mvs[0].l}, t=${mvs[0].t}, x=${mvs[0].x}, y=${mvs[0].y}`
    );
    console.log(`mvs: ${JSON.stringify(mvs)}`);

    for (let i = 0; i < 3; i++) {
        const action = g.suggest_action();
        if (action !== null) {
            if (g.has_child(action)) throw new Error('Suggestion already exists');
            if (g.visit_child(action)) throw new Error('Navigation created a missing child');
            if (!g.add_child(action)) throw new Error('Insertion failed');
            if (!g.has_child(action)) throw new Error('Inserted child is missing');
            if (g.add_child(action)) throw new Error('Duplicate insertion succeeded');
        }
    }

    const suggestions = g.get_child_actions();
    console.log('Suggested actions:', suggestions);
    console.log(JSON.stringify(suggestions[0].action));

    // Visit the first suggested move
    if (!g.visit_child(suggestions[0].action)) throw new Error('Existing navigation failed');
    g.visit_parent();
    if (!g.delete_child(suggestions[0].action)) throw new Error('Deletion failed');
    if (g.has_child(suggestions[0].action) || g.visit_child(suggestions[0].action)) {
        throw new Error('Deleted continuation reappeared');
    }

    // Show PGN and boards
    console.log('PGN after suggestion:', g.show_pgn(engine_wasm.SHOW_NOTHING, true, false));
    console.log('Current boards:', g.get_current_boards());
  } catch (err) {
    console.error('Error loading module:', err);
    process.exitCode = 1;
  }
}

// Run the test
runTest();

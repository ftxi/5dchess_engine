import sys
sys.path.append('build')
import engine
import time
import threading

# Test with 4x4 board (Very Small) at deeper depth
pgn = '[Board "Very Small - Open"]'
g = engine.game.from_pgn(pgn)

print('Testing Very Small - Open with perft_dynamic')
print('=' * 60)

# Only test depth 4 (safe depth)
for depth in [3, 4]:
    print(f'Depth {depth}:', end=' ', flush=True)
    start = time.time()
    count = g.perft_dynamic(depth, 0)
    elapsed = time.time() - start
    rate = count / elapsed if elapsed > 0 else 0
    print(f'{count:,} nodes in {elapsed:.2f}s ({rate:,.0f} nodes/s)')

# Estimate for depth 5
print()
print('Note: Depth 5+ would take hours due to state space > 10^11')
print('Use the UI with a timeout if you need to explore deeper depths.')

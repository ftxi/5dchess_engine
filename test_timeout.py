import sys
sys.path.append('build')
import engine
import time

# Test perft_timed with timeout
pgn = '[Board "Very Small - Open"]'
g = engine.game.from_pgn(pgn)

print('Testing perft_timed with timeout')
print('=' * 60)

# Test 1: Short search that completes
print('\nTest 1: Depth 3 with 10s timeout (should complete)')
start = time.time()
count, completed = g.perft_timed(3, 10.0, 0)
elapsed = time.time() - start
print(f'  Result: {count:,} nodes, completed={completed}, time={elapsed:.2f}s')

# Test 2: Short search that completes
print('\nTest 2: Depth 4 with 10s timeout (should complete)')
start = time.time()
count, completed = g.perft_timed(4, 10.0, 0)
elapsed = time.time() - start
print(f'  Result: {count:,} nodes, completed={completed}, time={elapsed:.2f}s')

# Test 3: Deep search with short timeout (should timeout)
print('\nTest 3: Depth 5 with 3s timeout (will timeout, partial result)')
start = time.time()
count, completed = g.perft_timed(5, 3.0, 0)
elapsed = time.time() - start
print(f'  Result: {count:,} nodes, completed={completed}, time={elapsed:.2f}s')

# Test 4: Deep search with slightly longer timeout
print('\nTest 4: Depth 5 with 5s timeout (will timeout, partial result)')
start = time.time()
count, completed = g.perft_timed(5, 5.0, 0)
elapsed = time.time() - start
print(f'  Result: {count:,} nodes, completed={completed}, time={elapsed:.2f}s')

print('\n' + '=' * 60)
print('Tests completed!')

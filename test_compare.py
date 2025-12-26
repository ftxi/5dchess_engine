import sys
sys.path.append('build')
import engine
import time

pgn = '[Board "Standard"]'
g = engine.game.from_pgn(pgn)

print('Testing Standard Chess perft with different modes:')
print('=' * 60)

# Test depth 3
depth = 3

print(f'Depth {depth}:')

# Single-threaded (basic perft)
start = time.time()
count1 = g.perft(depth)
t1 = time.time() - start
print(f'  perft (single):    {count1:,} nodes in {t1:.3f}s')

# Parallel
start = time.time()
count2 = g.perft_parallel(depth, 0)
t2 = time.time() - start
print(f'  perft_parallel:    {count2:,} nodes in {t2:.3f}s')

# Dynamic
start = time.time()
count3 = g.perft_dynamic(depth, 0)
t3 = time.time() - start
print(f'  perft_dynamic:     {count3:,} nodes in {t3:.3f}s')

print()
if count1 == count2 == count3:
    print('All counts match!')
else:
    print(f'COUNT MISMATCH: single={count1}, parallel={count2}, dynamic={count3}')

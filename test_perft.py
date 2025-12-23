import sys
import os

# Add the 'build' directory to sys.path
script_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(script_dir, 'build'))

import engine
import time

def test_variant(name, pgn, max_depth=4):
    print(f"\n{'='*70}")
    print(f"Testing: {name}")
    print(f"PGN: {pgn}")
    print('='*70)
    
    g = engine.game.from_pgn(pgn)
    
    # Test various depths
    for depth in range(1, max_depth + 1):
        print(f"\n--- Depth {depth} ---")
        
        # Single-threaded
        start = time.time()
        count_single = g.perft(depth)
        time_single = time.time() - start
        
        # Parallel without TT
        start = time.time()
        count_parallel = g.perft_parallel(depth)
        time_parallel = time.time() - start
        
        # Dynamic work-stealing
        start = time.time()
        count_dynamic = g.perft_dynamic(depth)
        time_dynamic = time.time() - start
        
        # Parallel with TT
        start = time.time()
        count_tt = g.perft_with_tt(depth)
        time_tt = time.time() - start
        
        rate_single = count_single/time_single if time_single > 0 else 0
        rate_parallel = count_parallel/time_parallel if time_parallel > 0 else 0
        rate_dynamic = count_dynamic/time_dynamic if time_dynamic > 0 else 0
        rate_tt = count_tt/time_tt if time_tt > 0 else 0
        
        print(f"Single:   {count_single:>12,} nodes in {time_single:>7.3f}s ({rate_single:>12,.0f} n/s)")
        print(f"Parallel: {count_parallel:>12,} nodes in {time_parallel:>7.3f}s ({rate_parallel:>12,.0f} n/s)")
        print(f"Dynamic:  {count_dynamic:>12,} nodes in {time_dynamic:>7.3f}s ({rate_dynamic:>12,.0f} n/s)")
        print(f"With TT:  {count_tt:>12,} nodes in {time_tt:>7.3f}s ({rate_tt:>12,.0f} n/s)")
        
        # Find best speedup
        best_time = min(time_parallel, time_dynamic, time_tt)
        if time_single > 0.001 and best_time > 0:
            print(f"Best speedup: {time_single/best_time:.2f}x")

# Test Very Small - Open (4x4 board) - this is the one that takes long
test_variant("Very Small - Open", '[Board "Very Small - Open"]', max_depth=4)

# Test Standard chess 
test_variant("Standard Chess", '[Board "Standard"]', max_depth=3)

print("\n" + "="*70)
print("All tests completed!")
print("="*70)

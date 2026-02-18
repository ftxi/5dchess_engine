import sys, os
original_sys_path = sys.path.copy()
try:
    sys.path.append(os.path.join(os.path.dirname(__file__), 'build'))
    import engine # type: ignore
finally:
    sys.path = original_sys_path

very_small_open = """
[Size "4x4"]
[Board "Custom"]
[Mode "5D"]
[nbrk/3p*/P*3/KRBN:0:1:w]
"""

g = engine.game.from_pgn(very_small_open)

print(g.get_current_boards())

fm = engine.ext_move(engine.vec4(0,1,1,0),engine.vec4(0,2,1,0))

print(g.apply_move(fm))
print(g.submit())

print(g.get_current_boards())
print(g.show_pgn(0))


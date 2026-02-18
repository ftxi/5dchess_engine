from flask import Flask, render_template, request
from flask_socketio import SocketIO, emit
import sys, os
original_sys_path = sys.path.copy()
try:
    sys.path.append(os.path.join(os.path.dirname(__file__), 'build'))
    import engine # type: ignore
finally:
    sys.path = original_sys_path

base_dir = os.path.abspath('extern/client')
static_dir = os.path.join(base_dir, 'static')
template_dir = os.path.join(base_dir, 'templates')

app = Flask(__name__, static_folder=static_dir, template_folder=template_dir)
socketio = SocketIO(app)

t0_fen = """
[Size "8x8"]
[Board "custom"]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:0:0:b]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:0:1:w]
"""
tminf_fen = """
[Size "8x8"]
[Board "custom"]
""" + '\n'.join([f'[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:0:{i}:b][r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:0:{i+1}:w]' for i in range(0, 41)])

g = engine.game.from_pgn(t0_fen)
game_data = {}
next_options = False

@app.route('/')
def index():
    return render_template('index.html')

qs = []
p0 = engine.vec4(0,0,0,0)
no_more_hint = False

@socketio.on('click')
def handle_click(data):
    l = int(data['l'])
    t = int(data['t'])
    c = int(data['c'])
    x = int(data['x'])
    y = int(data['y'])

    c1 = "wb"[data['c']]
    x1 = chr(data['x']+ord('a'))
    y1 = chr(data['y']+ord('1'))
    print(f"Received mouse click: ({l}T{t}{c1}){x1}{y1}")

    global qs, p0, g
    pos = engine.vec4(x,y,t,l)
    present_t, present_c = g.get_current_present()
    # print(f"present: t={present_t}, c={present_c}")
    # print(pos, qs, pos in qs)
    if pos in qs:
        fm = engine.ext_move(p0, pos)
        print("applying", fm)
        flag = g.apply_move(fm)
        print("finished", flag)
        hl = []
        if flag:
            if g.currently_check():
                checks = g.get_current_checks()
                arrows = []
                for p, q in checks:
                    arrows.append({
                        'from': {'l':p.l(), 't': p.t(), 'x':p.x(), 'y':p.y(), 'c':1-present_c},
                        'to': {'l':q.l(), 't': q.t(), 'x':q.x(), 'y':q.y(), 'c':1-present_c},
                    })
                    print('piece on', p, 'is checking', q)
                hl = [
                    {
                        'color':'#ff1111',
                        'arrows':arrows
                    },
                ]
                print('applying move ', fm, ' --success (checking)')
            else:
                print('applying move ', fm, ' --success')
        else:
            print('applying move ', fm, ' --failure')
        qs = []
        display(hl)
    elif c == present_c:
        qs = g.gen_move_if_playable(pos)
        #print('ds = ', ds)
        moves = [{'x':q.x(), 'y':q.y(), 't':q.t(), 'l':q.l(), 'c':present_c} for q in qs]
        hl = [
            {
                'color':'#ff80c0',
                'coordinates':[{'x':pos.x(), 'y':pos.y(), 't':pos.t(), 'l':pos.l(), 'c':c}]
            },
            {
                'color': '#80ff80',
                'coordinates': moves
            }
        ]
        p0 = pos
        display(hl)
    else:
        print('no piece at click')
        qs = []
        display()

@socketio.on('right_click')
def handle_click(data):
    print('canceled click')
    global qs
    qs = []
    display()

@socketio.on('request_prev')
def handle_prev():
    print('load previous move')
    global no_more_hint
    no_more_hint = False
    g.visit_parent()
    display()

@socketio.on('request_next')
def handle_next(data):
    print('load next move')
    global next_options, no_more_hint
    no_more_hint = False
    g.visit_child(next_options[data])
    display()

@socketio.on('request_undo')
def handle_undo():
    print('attempting undo', end='')
    flag = g.undo()
    print(' ---', 'success' if flag else 'failed')
    display()

@socketio.on('request_redo')
def handle_redo():
    print('attempting redo', end='')
    flag = g.redo()
    print(' ---', 'success' if flag else 'failed')
    display()

@socketio.on('request_submit')
def handle_submit():
    print('received submition request', end='')
    flag = g.submit()
    global no_more_hint
    no_more_hint = False
    print(' ---', 'success' if flag else 'failed')
    display()

@socketio.on('request_hint')
def suggest_action():
    print('received hint request', end='')
    flag = g.suggest_action()
    global no_more_hint
    no_more_hint = not flag
    print(' ---', 'success' if flag else 'failed')
    display()

@socketio.on('request_load')
def suggest_action(data):
    print('received load:')
    print(data)
    global g
    try:
        g = engine.game.from_pgn(data)
        display()
    except RuntimeError as e:
        emit('response_load', str(e))

def convert_boards_data(boards):
    def convert_board(board):
        l, t, c, s = board
        return {"l":l, "t":t, "c":c, "fen":s}
    return list(map(convert_board, boards))

def display(hl=[]):
    mandatory, optional, unplayable = g.get_current_timeline_status()
    present_t, present_c = g.get_current_present()
    critical = g.get_movable_pieces()
    cc = [{'x':q.x(), 'y':q.y(), 't':q.t(), 'l':q.l(), 'c':present_c} for q in critical]
    match_status = g.get_match_status()
    comments = g.get_comments()
    if comments:
        emit('response_text', comments[-1])
    else:
        emit('response_text', "no comments")
    size_x, size_y = g.get_board_size()
    children = list(enumerate(g.get_child_actions()))
    global next_options, no_more_hint
    select_values = {str(n):s for (n,(_,s)) in children}
    next_options = {str(n):a for (n,(a,_)) in children}
    def show_status(ms):
        if ms == engine.match_status_t.PLAYING:
            if present_c:
                return "Black's move"
            else:
                return "White's move"
        elif ms == engine.match_status_t.WHITE_WINS:
            return "White wins"
        elif ms == engine.match_status_t.BLACK_WINS:
            return "Black wins"
        elif ms == engine.match_status_t.STALEMATE:
            return "Stalemate"
        else:
            return str(ms)

    new_data = {
        'submit-button': 'enabled' if g.can_submit() else 'disabled',
        'prev-button': 'enabled' if g.has_parent() else 'disabled',
        'next-button': 'enabled' if select_values else 'disabled',
        'undo-button': 'enabled' if g.can_undo() else 'disabled',
        'redo-button': 'enabled' if g.can_redo() else 'disabled',
        'hint-button': 'enabled' if not no_more_hint else 'disabled',
        'next-options': select_values,
        'metadata': {
            "mode" : "odd"
        },
        'match-status': show_status(match_status),
        'size': {
            'x':size_x,
            'y':size_y
        },
        'present': {
            't': present_t,
            'c': present_c,
            'color': 'rgba(219,172,52,0.4)'# if not is_game_over else 'rgba(128,128,128,0.4)'
        },
        'focus': [
            {
                'l': line,
                't': present_t,
                'c': present_c
            } for line in mandatory
        ],
        'boards':convert_boards_data(g.get_current_boards()),
        'highlights':[
            {
                'color':'#7070ff',
                'timelines': mandatory,
            },
            {
                'color':'#80ff80',
                'timelines': optional,
            },
            {
                'color':'#ffaaaa',
                'timelines': unplayable,
            },
            {
                'color':'#a569bd',
                'coordinates': cc,
            }
        ] + hl
    }
    game_data.update(new_data)
    #print('displaying ', g.get_current_boards())
    emit('response_data', game_data)

@socketio.on('request_data')
def handle_request(data):
    display()

@socketio.on('request_pgn')
def handle_undo():
    print('client requests pgn')
    pgn = g.show_pgn(engine.SHOW_ALL & ~engine.SHOW_RELATIVE)
    emit('response_pgn', pgn)

if __name__ == '__main__':
    socketio.run(app, debug=True)

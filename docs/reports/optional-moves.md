# Pruning optional moves

This investigation asks whether physical moves on optional timelines and
nonbranching jumps between optional timelines can be pruned. “Optional” refers
to the timeline's status at the start of the action. A jump's departure and
arrival must be considered together.

## Questions and analysis

1. **Does pruning preserve a legal action whenever full search has one?**
   Blanket semimove omission fails. An optional move can advance a destination
   so a later jump branches, or advance an inactive timeline that a new branch
   activates. The examples below exhibit both dependencies.
2. **Can complete actions containing unnecessary optional moves be pruned?**
   For current-action existence, remove a unit only when the remaining action
   preserves branch structure and successful submission. With the branch list
   fixed, retain a unit if a branch targets an affected original endpoint, or
   an affected timeline is finally active and its original endpoint precedes
   the final Present. Removing other units erases opponent-sided result boards
   without introducing a royal capture. Checked replay can admit further
   reductions that change branching. These arguments preserve a legal witness.
3. **If the current turn would end in stalemate, can isolated optional moves
   rescue it and checkmate the opponent?**
   Distinguish having no legal action from playing a legal action that
   stalemates the opponent. Preserving submission does not preserve the latter
   outcome: the mate-in-two example below adds an optional move to a drawing
   action and turns it into a forced win.
4. **If the opponent can checkmate next turn, are isolated optional moves
   unnecessary because they can be played later and give the opponent more boards?**
   No. A move can be unnecessary for submitting this turn and still change a
   forced loss into a draw. Mandatory status and the check test change on the
   next turn. The complete drawing strategy below supplies the counterexample.

The earlier legality diagnostic checked 504,615 actions, including custom
setups. Every structural reduction submitted legally. On 32 opening positions,
23 fully enumerated and nine capped at 50,000 actions, the structural rule kept
158,426 of 504,159 inspected actions. Additional checked deletion reduced that
count to 154,472, matching blanket pruning's count on that sample. These
current-action results motivated the strategic question answered below.

## Dependencies on new timelines

The following strictly accepted ordinary-piece setups record the earlier
counterexamples. Their continuations end after legal submission, with the game
still ongoing. The first ordinary-piece example also reproduces the dependency
previously found in a 4×4 setup containing a unicorn.

### Advancing a branching destination

```5dpgn
[Board "custom"]
[Size "8x8"]
[Timeline "Odd"]
[7k/8/8/8/2N5/8/1rbR4/Kq6:0:2:w]
[8/8/8/8/2kb4/2N5/1K6/2rR4:1:3:w]

1w. (1T3)Kb2a3 (0T2)Ka1>>(1T3)a2
{Legal submission. The optional move makes White T3 on timeline 1 historical,
so the mandatory jump creates timeline 2. Omitting Kb2a3 makes the jump
nonbranching and permits the black queen on (0T2)b1 to capture the king on b2.
Full search has one legal action. Blanket optional pruning has none.}
```

### Advancing a newly activated timeline

```5dpgn
[Board "custom"]
[Size "8x8"]
[Timeline "Odd"]
[7k/8/8/8/2N5/8/1rbR4/Kq6:0:2:w]
[7k/8/8/8/8/8/8/K7:-1:4:b]
[8/8/8/8/2kb4/2N5/1K6/2rR4:1:3:w]
[7k/8/8/8/8/8/8/K7:-2:4:b]
[7k/8/8/8/8/8/8/K7:-3:1:w]

1w. (-3T1)Ka1a2 (1T3)Kb2a3 (0T2)Ka1>>(1T3)a2 

{Legal submission at Black T1. The branch activates timeline -3.
Omitting its optional Ka2 leaves the Present at White T1 and prevents submission.
Omitting the move on timeline 1 instead causes the queen exposure above.
All three legal actions need both optional moves.}
```

### Optional-to-optional nonbranching jump

```5dpgn
[Board "custom"]
[Size "8x8"]
[Timeline "Odd"]
[7k/8/8/8/2N5/8/1rbR4/Kq6:0:2:w]
[8/8/8/8/2kb4/2N5/1K6/2rR4:1:3:w]
[7k/8/8/8/8/8/8/7K:2:3:w]

1w. (1T3)Kb2>(2T3)a2 (0T2)Ka1>>(1T3)a2 

{Legal submission. The optional nonbranching jump advances both timelines 1 and 2.
It makes the later mandatory jump branch. Omitting it makes that jump
nonbranching and exposes the king. Blanket optional pruning finds no action.}
```

## An isolated move saves a draw

The custom 8×8 position below uses ordinary pieces. Empty separating timelines
are accepted by strict setup validation. Reachability from the standard opening
was not established.

Full search finds six initial White actions. After `Ka1` alone, Black `Kc1`
checkmates. Each of the other five White actions permits Black `Kc1` to obtain
stalemate, so Black can prevent a White win. The tree records one drawing White
action, all six Black replies, a White response to each nonterminal reply, and
every subsequent Black action. All 32 leaves of that strategy are stalemate.
Thus the full position has value draw. Optional pruning retains only `Ka1`,
whose value is loss.

```5dpgn
[Board "custom"]
[Size "8x8"]
[Timeline "Odd"]
[8/8/8/8/1p6/1p6/K1k5/8:0:1:w]
[8/8/8/8/8/8/8/8:-1:5:b]
[8/8/8/8/8/8/8/8:1:5:b]
[k1r5/3Q4/1PK5/8/8/8/8/8:2:2:w]

{White T1. Timeline 0 is mandatory, timeline 2 is optional.
The empty Black T5 boards separate the playable timelines.}
(1w. (0T1)Ka2a1 {Omitting the isolated optional move leaves the only action retained by pruning.}
  1b. (0T1)Kc2c1
  {White T2. Timeline 0 has no move. Timeline 2 is now mandatory and its king
  on c6 is checked by Rc8. No complete legal action exists. White is checkmated.}
  0-1)
(1w. (0T1)Ka2a1 (2T2)Qd7c7
  1b. (0T1)Kc2c1 {Black can secure stalemate against this alternative too.} 1/2-1/2)
(1w. (0T1)Ka2a1 (2T2)Kc6d6
  1b. (0T1)Kc2c1 1/2-1/2)
(1w. (0T1)Ka2a1 (2T2)Kc6d5
  1b. (0T1)Kc2c1 1/2-1/2)
(1w. (0T1)Ka2a1 (2T2)Kc6b5
  1b. (0T1)Kc2c1 1/2-1/2)
1w. (0T1)Ka2a1 (2T2)Qd7xc8
{The optional capture resolves the local rook threat and leaves timeline 2
on Black T2. Both White actions above leave the Present at Black T1.
All six Black replies follow. White selects a drawing reply where required.}
(1b. (0T1)b3b2
  2w. (0T2)Ka1a2
    (2b. (0T2)Kc2>>(0T1)d3 {Stalemate on the new active timeline -2.} 1/2-1/2)
    (2b. (0T2)Kc2>>(0T1)c3 1/2-1/2)
    (2b. (0T2)Kc2>>(0T1)d2 1/2-1/2)
    (2b. (0T2)Kc2>>(0T1)d1 1/2-1/2)
    2b. (0T2)Kc2>>(0T1)c1 1/2-1/2)
(1b. (0T1)Kc2d3
  2w. (0T2)Ka1b2
    (2b. (0T2)Kd3>>(0T1)e4 {Stalemate on the new active timeline -2.} 1/2-1/2)
    (2b. (0T2)Kd3>>(0T1)d4 1/2-1/2)
    (2b. (0T2)Kd3>>(0T1)c4 1/2-1/2)
    (2b. (0T2)Kd3>>(0T1)e3 1/2-1/2)
    (2b. (0T2)Kd3>>(0T1)d3 1/2-1/2)
    (2b. (0T2)Kd3>>(0T1)c3 1/2-1/2)
    (2b. (0T2)Kd3>>(0T1)e2 1/2-1/2)
    2b. (0T2)Kd3>>(0T1)d2 1/2-1/2)
(1b. (0T1)Kc2c3
  2w. (0T2)Ka1b1
    (2b. (0T2)Kc3>>(0T1)d4 {Stalemate on the new active timeline -2.} 1/2-1/2)
    (2b. (0T2)Kc3>>(0T1)c4 1/2-1/2)
    (2b. (0T2)Kc3>>(0T1)d3 1/2-1/2)
    (2b. (0T2)Kc3>>(0T1)c3 1/2-1/2)
    2b. (0T2)Kc3>>(0T1)d2 1/2-1/2)
(1b. (0T1)Kc2d2
  2w. (0T2)Ka1b2
    (2b. (0T2)Kd2>>(0T1)e3 {Stalemate on the new active timeline -2.} 1/2-1/2)
    (2b. (0T2)Kd2>>(0T1)d3 1/2-1/2)
    (2b. (0T2)Kd2>>(0T1)c3 1/2-1/2)
    (2b. (0T2)Kd2>>(0T1)e2 1/2-1/2)
    (2b. (0T2)Kd2>>(0T1)d2 1/2-1/2)
    (2b. (0T2)Kd2>>(0T1)e1 1/2-1/2)
    (2b. (0T2)Kd2>>(0T1)d1 1/2-1/2)
    2b. (0T2)Kd2>>(0T1)c1 1/2-1/2)
(1b. (0T1)Kc2d1
  2w. (0T2)Ka1b2
    (2b. (0T2)Kd1>>(0T1)e2 {Stalemate on the new active timeline -2.} 1/2-1/2)
    (2b. (0T2)Kd1>>(0T1)d2 1/2-1/2)
    (2b. (0T2)Kd1>>(0T1)e1 1/2-1/2)
    (2b. (0T2)Kd1>>(0T1)d1 1/2-1/2)
    2b. (0T2)Kd1>>(0T1)c1 1/2-1/2)
1b. (0T1)Kc2c1 {Only timeline 0 is mandatory at White T2. Ka1 has no move and is not in check. Stalemate.} 1/2-1/2
```

The isolated `Qxc8` creates no timeline and leaves the same Present as `Ka1`
alone. After Black `Kc1`, playing it later cannot complete an action because
timeline 0 is immobilized and the game has ended. Resolving the threat one turn
earlier removes timeline 2 from White's next mandatory set and changes checkmate
to stalemate.

The tree was checked with full Deferred and Stable HC search. Checked move
application, submission, and strict PGN reload confirmed the terminal results.
The drawing strategy contains 43 nodes and has no unresolved leaves.

## An optional move enables a win

The following two-timeline quiz is mate in two White actions, with no mate in
one. Timeline 0 is mandatory at White T1; timeline -1 is optional at White T2.
The `P*` markers are intentional for compatibility with variant loaders.

Playing only `(0T1)f6f7` immediately stalemates Black. Adding the quiet optional
move `(-1T2)Ne7c8` instead permits `Kh8g7`, followed by `Ng8e7#`. Both first
actions are nonbranching and submit with the Present at Black T1. The optional
move is unnecessary for legal submission but changes this action's outcome
from draw to forced win.

```5dpgn
[Size "8x8"]
[Timeline "Odd"]
[6Nk/7P*/4BP*BR/3NP*1P*1/8/8/8/1K6:0:1:w]
[5Rnb/4N1RP/8/p7/8/P7/8/8:-1:1:w]
[6Rb/4N1RP/8/p7/8/P7/8/8:-1:1:b]
[6Rb/4N1RP/8/8/p7/P7/8/8:-1:2:w]

(1w. (0T1)f6f7 {Black has no legal action and is not in check.} 1/2-1/2)
1w. (-1T2)Ne7c8 (0T1)f6f7
{Advancing the optional endpoint removes Rg7's control of Black's escape
square on timeline 0. Black has exactly four legal replies.}
(1b. (0T1)Kh8g7 (-1T2)Bh8xg7
  2w. (0T2)Ng8e7# 1-0)
(1b. (0T1)Kh8g7 (-1T2)Bh8>>(-1T1)g8
  2w. (0T2)Ng8e7# 1-0)
(1b. (0T1)Kh8g7 (-1T2)Bh8>>(-1T1)h7
  2w. (0T2)Ng8e7# 1-0)
1b. (0T1)Kh8g7
2w. (0T2)Ng8e7# 1-0
```

The final knight attacks the historical king on `(0T2)g7` from `(0T3)e7`,
across two files and one time step. This prevents escaping the attack by
moving the king again. Full action enumeration found 1,515 initial White
actions and no mate in one. All four Black replies to the displayed winning
action were enumerated; the same White response checkmates after each.
Checked move application, submission, and strict PGN reload verified the
stalemate alternative and all four winning continuations.

This compares two particular first actions; alternative branching solutions
also exist. It does not establish that every winning first action must move
from the optional endpoint. Nor does it refute FirePlank commit `543fe4d`:
that commit's optional-move rejection requires a branching action, so it
retains the displayed nonbranching winning action.

## Consequence for pruning

Preserving current legal-action existence does not preserve game value. Both
the structural and checked-deletion rules discard the drawing `Qxc8`, because
`Ka1` alone still submits legally. The terminal fallback also misses this case
because the pruned space already contains that losing legal action. Optional
pruning therefore remains a lossy gameplay heuristic unless stronger strategic
conditions are established.

# Iteration-ratio policy matches

`iteration_ratio_match.py` is an early policy-quality test. It asks how many
of `zero`'s evaluated MCTS paths a target policy needs to compete with `zero`,
without requiring the target's first implementation to have production IPS.
It tests descending iteration multipliers before an equal-time Elo run.

This is deliberately not a compute-equivalent benchmark. An expensive target
iteration is allowed more wall time than a `zero` iteration. The experiment
measures decision quality per completed MCTS iteration and separately reports
the throughput deficit that later optimization must recover.

## Basic usage

Build the command-line engine and the Python rules module first. The Python
interpreter running the script must match the interpreter ABI used to build
the module in `build/`.

From the repository root:

```sh
python3.12 iteration_ratio_match.py \
  --target './build/5dchess factored --seed {seed}' \
  --ratios 1,.75,.5,.25 \
  --workers 4
```

The ratios must be unique and descending. The default sequence is `1`,
`0.75`, `0.5`, and `0.25`. A ratio passes before the script tries the next,
smaller ratio. Significant inferiority or an inconclusive maximum sample stops
the sweep.

`{seed}` in either engine command is replaced with the pair's seed. If the
placeholder is absent, the command is used unchanged. Pair seeds begin at
`--seed-start`, which defaults to 1.

Use repeated `--opening` options to cycle through fixed starting positions:

```sh
python3.12 iteration_ratio_match.py \
  --target './build/5dchess candidate --seed {seed}' \
  --opening openings/quiet.5dpgn \
  --opening openings/branched.5dpgn \
  --workers 4
```

Without `--opening`, games start from the standard turn-zero position.

Important options are:

| Option | Default | Meaning |
|---|---:|---|
| `--zero-movetime` | 400 ms | Clock for normal `zero` moves and position meters |
| `--cap-factor` | 2 | Safety multiplier for the target wall-time cap |
| `--max-target-movetime` | 10000 ms | Absolute target cap per action |
| `--workers` | 2 | Concurrent games, not process count |
| `--max-actions` | 500 | Draw cap per game |
| `--look-pairs` | 8,16,32,64,128,256 | Predeclared statistical looks |
| `--max-pairs` | 256 | Maximum color-reversed pairs at one ratio |
| `--alpha` | 0.05 | Total within-ratio sequential error budget |
| `--noninferiority-margin` | 0.10 | Tolerated deficit in normalized pair score |

Each active game uses three engine processes: the two players and a separate
`zero` meter. Consequently `--workers 4` can run twelve engine processes.
Parallel load affects measured IPS, so keep worker count fixed when comparing
experiments.

Ctrl-C cancels active games, closes their engines, excludes unfinished games,
and prints a summary containing the smallest ratio already established.

## Iteration and time budgets

On a normal `zero` turn, `zero` receives `T0 = --zero-movetime`. On every
target turn, a separate `zero` process first searches the exact same position
for `T0`. If this meter completes `N0` iterations, the requested target budget
at multiplier `m` is

```text
N_requested = max(1, ceil(m * N0)).
```

The engine's current `go depth` interface converts one depth unit to ten MCTS
iterations. The script therefore sends

```text
depth = max(1, ceil(N_requested / 10))
N_limit = 10 * depth.
```

The target normally receives between `N_requested` and `N_requested + 9`
iterations. `searches.csv` records both values.

The target also receives a wall-time limit. Define the measured throughput
ratio as

```text
r = target IPS / zero IPS.
```

With cap factor `f`, zero time `T0`, and absolute maximum `Cmax`, the cap is

```text
C = max(T0, min(Cmax, ceil(f * T0 / r))).
```

The inverse dependence on `r` is intentional. A target running at half of
`zero`'s IPS needs about twice as much time for an equal iteration count. The
factor of two absorbs position variance; `Cmax` prevents a pathological search
from stalling the series. The multiplier `m` is not included in the cap, so
smaller iteration ratios retain at least as much safety margin.

An initial fixed-time calibration supplies the first `r`. After target moves,
the cap uses the rolling median of the last 64 exact-position target/meter IPS
ratios. The final report additionally gives:

- aggregate target IPS;
- aggregate zero IPS;
- aggregate target/zero IPS;
- the geometric mean of paired exact-position IPS ratios.

If the target hits its time cap, the game continues with the iterations it
actually completed. The row is marked `time_capped=1`; it is not silently
discarded.

## Reading live output

For example:

```text
[m=0.75] games 138: target 80-0-53 (void 5); pairs 19-38-7
(void 5, mean score 0.594); target/zero IPS 0.752
```

The fields mean:

- `m=0.75`: target iteration budget is 75% of the same-position `zero` count.
- `games 138`: 138 games have finished, including void games.
- `target 80-0-53`: target game wins, draws, and losses. A completed game can
  appear here before its color-reversed partner finishes.
- game `void 5`: five games were excluded because the experiment could not
  obtain a fair result, such as a meter or controller failure. A void is not a
  draw.
- `pairs 19-38-7`: pair wins, pair ties, and pair losses. The middle number is
  a two-game pair tie, not an individual-game draw.
- pair `void 5`: five pairs were excluded because at least one constituent
  game was void.
- `mean score 0.594`: cumulative normalized target score over complete valid
  pairs.
- `target/zero IPS 0.752`: aggregate target IPS is currently 75.2% of
  aggregate `zero` IPS.

Actual competing-engine failures are scored competitively: a target failure
is a target loss and a playing `zero` failure is a target win. Meter and
controller failures are void. Ctrl-C unfinished games are neither completed
nor void; they are omitted.

## Color-reversed pairs

One statistical unit is a predeclared pair of games with the same opening and
seed:

1. target plays White and `zero` plays Black;
2. `zero` plays White and target plays Black.

For one game, target receives 1 point for a win, 0.5 for a draw, and 0 for a
loss. If the two game scores are `a_i` and `b_i`, the normalized score of pair
`i` is

```text
q_i = (a_i + b_i) / 2.
```

Possible individual pair scores are:

| Results in the pair | `q_i` | Classification |
|---|---:|---|
| two wins | 1.00 | pair win |
| win and draw | 0.75 | pair win |
| win and loss | 0.50 | pair tie |
| two draws | 0.50 | pair tie |
| draw and loss | 0.25 | pair loss |
| two losses | 0.00 | pair loss |

For `n` complete valid pairs, the displayed mean score is

```text
q_bar = (q_1 + ... + q_n) / n
      = total target game points / (2 * n).
```

Although one pair has only five possible scores, the cumulative mean has
increments of `0.25 / n`. For example, pair scores `1`, `0.5`, and `0.25`
produce mean score `0.5833`.

If there are `n` target-as-White results and `n` target-as-Black results, this
mean is invariant under any permutation used to pair the colors. Pair W-T-L is
not invariant. For example, White scores `[1, 0]` and Black scores `[1, 0]`
can be paired into one pair win and one pair loss, or into two pair ties. Both
pairings have mean score 0.5, but their sign-test inputs differ.

The script never chooses a pairing after seeing results. Opening, seed, and
pair identity are fixed before play. This blocking is meaningful only when
the paired games share relevant nuisance conditions. The p-values also assume
different pairs are independent experimental units. Repeating an effectively
deterministic identical pair does not create independent evidence; use varied
openings and seeds.

## Statistical hypotheses and p-values

The game-level target W-D-L display is descriptive. Statistical tests use
complete color-reversed pairs.

### Superiority

Let `W`, `T`, and `L` be pair wins, ties, and losses. The exact sign test uses
only the `D = W + L` decisive pairs. Under the null hypothesis that pair wins
and losses are equally likely,

```text
p_superior = sum(k=W..D) choose(D, k) * 0.5^D.
```

Pair ties are excluded. A small value supports a target tendency to win a
random matched block more often than it loses one.

### Inferiority

The opposite exact sign-test tail is

```text
p_inferior = sum(k=0..W) choose(D, k) * 0.5^D.
```

A small value supports a target tendency to lose matched blocks more often
than it wins them.

### Non-inferiority

A p-value cannot establish exact equality. The script operationalizes
"equal or better" as non-inferiority within a declared margin `delta`. With
the default `delta = 0.10`, the null boundary for normalized mean score is

```text
p0 = 0.5 - delta = 0.4.
```

All valid pair scores, including pair ties, enter the mean. Because every
`q_i` lies in `[0, 1]`, the script uses the distribution-free Hoeffding bound

```text
p_noninferior = exp(-2 * n * (q_bar - p0)^2), if q_bar > p0
                1,                              otherwise.
```

This is a conservative valid p-value bound. It requires no parametric model
for pair scores, but it can require many pairs when the true score is close to
0.5 or when a narrower margin is requested.

## Sequential looks and the displayed threshold

Repeatedly stopping whenever an ordinary p-value falls below 0.05 would
inflate the false-positive rate. The script tests only at the predeclared pair
counts supplied by `--look-pairs`.

For planned looks `L_1, ..., L_j` and total within-ratio alpha `alpha`, the
p-value threshold at look `L_i` is

```text
alpha_i = alpha * L_i / (L_1 + ... + L_j).
```

The thresholds sum to `alpha`. With the defaults, the denominator is
`8 + 16 + 32 + 64 + 128 + 256 = 504`:

| Valid pairs | Threshold |
|---:|---:|
| 8 | 0.000794 |
| 16 | 0.001587 |
| 32 | 0.003175 |
| 64 | 0.006349 |
| 128 | 0.012698 |
| 256 | 0.025397 |

Later looks receive more alpha because they have more statistical power. At a
look, the decision order is:

1. `better` if both superiority and non-inferiority p-values are at or below
   the current threshold;
2. `noninferior` if the non-inferiority p-value is at or below the threshold;
3. `inferior` if the inferiority p-value is at or below the threshold;
4. otherwise continue to the next look.

If the final look finishes without a decision, the ratio is `inconclusive`
and the multiplier sweep stops. A `better` or `noninferior` ratio passes, and
the script begins the next smaller multiplier. The final "most aggressive
established ratio" is the smallest multiplier that passed; it is not the
currently running or merely encouraging multiplier.

The alpha budget controls repeated looks within one multiplier. It resets for
the next multiplier and does not provide an additional family-wise correction
over the entire ratio sweep. Claims comparing several tested multipliers must
account for that limitation.

### Worked example

For

```text
pairs 19-38-7, mean score 0.59375, n=64
```

there are `D = 19 + 7 = 26` decisive pairs. The exact upper sign-test tail is
`p_superior = 0.01448`, while `p_inferior = 0.9953`. With the default
non-inferiority boundary 0.4,

```text
p_noninferior = exp(-2 * 64 * (0.59375 - 0.4)^2)
                = 0.008189.
```

The 64-pair sequential threshold is

```text
0.05 * 64 / 504 = 0.006349.
```

Neither `0.01448` nor `0.008189` crosses the threshold, and the inferiority
p-value is large, so the result remains undecided and play continues.

## Output files

The default output directory is
`logs/iteration-ratio-YYYYmmdd-HHMMSS/`. It contains:

- one complete PGN for every finished game;
- `games.csv`, containing ratio, pair, seed, target color, outcome, action
  count, termination reason, and PGN path;
- `searches.csv`, containing the zero-meter iteration count, meter elapsed
  time and IPS, requested and rounded target limits, IPS estimate used for the
  cap, wall cap, actual target iterations, elapsed time, IPS, and cap status.

The CSV files are flushed after each row, so completed work remains available
after Ctrl-C.

## Interpretation

Passing `m=1` says the target is competitive with `zero` when given roughly
the same number of evaluated MCTS paths. It does not say the target is already
competitive at equal time. If the target needs multiplier `m_star` and runs at
throughput ratio `r`, equal-time viability roughly requires

```text
r >= m_star.
```

The iteration-ratio experiment is an early gate. A successful candidate still
needs profiling and optimization, followed by fixed-clock games at the
production 1500 ms budget.

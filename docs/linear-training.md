# Training the linear evaluation

The training framework fits exactly the bounded model used by
`linear_engine::evaluate()` and produces checkpoints that the command-line
engine can load directly. Its public API is in
`src/engine/linear_training.h`; the feature definitions are in
[Linear evaluation features](linear-features.md).

Although the evaluator is called linear, only its unbounded score is linear.
A hyperbolic tangent maps that score into the same `[-1, 1]` interval as MCTS
terminal values. The resulting method is weighted nonlinear regression with a
linear predictor and a fixed `tanh` link.

## Notation

Let the training set contain `N` examples. For example `i`:

| Symbol | Meaning |
|---|---|
| $D$ | Feature count, currently `84` |
| $x_i \in \mathbb{R}^D$ | Player-to-move feature vector |
| $x_{i0}=1$ | Bias feature |
| $w \in \mathbb{R}^D$ | Trainable weights, including bias $w_0$ |
| $r_i \in [-1,1]$ | White-centric result or teacher score |
| $q_i \in \{+1,-1\}$ | `+1` if White moves, `-1` if Black moves |
| $y_i=q_i r_i$ | Player-to-move training target |
| $a_i>0$ | Sample weight |
| $\lambda\ge0$ | L2 coefficient |

Hard game-result targets use $r_i=+1$ for a White win, $r_i=-1$ for a Black
win, and $r_i=0$ for a draw. Soft teacher evaluations may use any finite value
in the same interval.

## Perspective conversion

Every feature is interpreted from the perspective of the player to move, so
the target must use the same perspective. The conversion is

$$
y_i =
\begin{cases}
r_i, & \text{White to move},\\
-r_i, & \text{Black to move}.
\end{cases}
$$

`linear_training_sample::from_position(position, white_target)` implements
this equation. For a game White eventually wins, successive positions receive
targets `+1, -1, +1, -1, ...` as the side to move alternates. Assigning `+1`
to every position would incorrectly teach the model that both players are
winning.

At inference time the inverse conversion produces the White-centric MCTS
score. If $p_i$ is the player-to-move prediction, the engine returns
$q_i p_i$. Therefore, when training succeeds,

$$
p_i \approx y_i=q_i r_i
\quad\Longrightarrow\quad
q_i p_i \approx q_i^2 r_i=r_i.
$$

The factory extracts the feature vector and rejects non-finite targets,
targets outside `[-1, 1]`, non-positive sample weights, non-finite features,
or a bias feature other than one.

```cpp
std::vector<linear_training_sample> samples;
samples.push_back(linear_training_sample::from_position(
    position, 1.0f));  // White eventually won this game.
```

## Forward model

For one position, the unbounded linear score is

$$
z_i = w^T x_i = \sum_{j=0}^{D-1} w_j x_{ij}.
$$

The normalized player-to-move prediction is

$$
p_i = \tanh(z_i).
$$

Thus $p_i\in(-1,1)$ for finite weights. The engine's `WINNING_SCORE` is one,
so no additional scale factor is needed. Training and runtime prediction both
call `linear_engine::predict_player_score()`, preventing the two paths from
drifting apart.

The `tanh` bound has two consequences:

- a terminal label of exactly `+1` or `-1` can only be approached, never
  reached with finite weights; and
- the derivative becomes small when $|p_i|$ is near one, so saturated examples
  learn slowly.

L2 regularization and validation checkpointing keep separable outcome data
from driving weights upward merely to move predictions closer to an
unreachable endpoint.

## Weighted objective

Define the total sample weight

$$
A=\sum_{i=1}^{N}a_i.
$$

The data loss is half the weighted mean squared error:

$$
L_{\text{data}}(w)
=\frac{1}{2A}\sum_{i=1}^{N}a_i(p_i-y_i)^2.
$$

The factor `1/2` does not change the optimum; it cancels the factor `2` when
differentiating the square. L2 regularization excludes the bias:

$$
L_{\text{reg}}(w)
=\frac{\lambda}{2}\sum_{j=1}^{D-1}w_j^2.
$$

The optimized and reported objective is

$$
L(w)=L_{\text{data}}(w)+L_{\text{reg}}(w).
$$

Excluding $w_0$ from regularization lets the model learn the dataset's base
rate without treating that intercept as feature complexity. In addition to
$L$, reports contain

$$
\operatorname{MSE}=\frac{1}{A}\sum_i a_i(p_i-y_i)^2
$$

and

$$
\operatorname{MAE}=\frac{1}{A}\sum_i a_i|p_i-y_i|.
$$

MSE and MAE exclude regularization; `objective_loss` includes it.

## Exact gradient

The required scalar derivatives are

$$
\frac{\partial p_i}{\partial z_i}
=1-\tanh^2(z_i)=1-p_i^2,
\qquad
\frac{\partial z_i}{\partial w_j}=x_{ij}.
$$

Applying the chain rule gives the data gradient

$$
\frac{\partial L_{\text{data}}}{\partial w_j}
=\frac{1}{A}\sum_{i=1}^{N}
a_i(p_i-y_i)(1-p_i^2)x_{ij}.
$$

Adding L2 produces the full gradient component

$$
\boxed{
\frac{\partial L}{\partial w_j}
=\frac{1}{A}\sum_{i=1}^{N}
a_i(p_i-y_i)(1-p_i^2)x_{ij}
+\lambda w_j\,\mathbf{1}_{j\ne0}
}
$$

where $\mathbf{1}_{j\ne0}$ is one for non-bias weights and zero for the bias.
`linear_trainer::loss_and_gradient()` evaluates this complete expression for
the span it receives.

Feature and stored weight values are `float`, matching inference checkpoints.
Dot products, losses, gradients, and Adam moments are accumulated in `double`;
each updated checkpoint weight is range-checked and converted back to `float`.

## Correct weighted mini-batches

Directly normalizing every batch by only its own sample-weight sum is wrong for
this optimizer: with a one-example batch, the example's $a_i$ would cancel and
sample weights would have no effect. The implementation instead constructs an
unbiased estimate of the complete data gradient.

For a shuffled batch $B$ with $b=|B|$ and
$A_B=\sum_{i\in B}a_i$, `loss_and_gradient(B, w, 0)` first returns

$$
h_B=\frac{1}{A_B}\sum_{i\in B}
a_i(p_i-y_i)(1-p_i^2)x_i.
$$

The trainer multiplies this by

$$
s_B=\frac{A_B N}{bA},
$$

so the gradient sent to Adam is

$$
\widehat g_B
=s_Bh_B+\lambda \widetilde w
=\frac{N}{bA}\sum_{i\in B}
a_i(p_i-y_i)(1-p_i^2)x_i
+\lambda \widetilde w,
$$

where $\widetilde w_0=0$ and $\widetilde w_j=w_j$ for $j\ne0$. For fixed
weights, a segment of a uniformly shuffled permutation is a uniform subset of
size $b$, hence

$$
\mathbb{E}\left[\sum_{i\in B}a_i u_i\right]
=\frac{b}{N}\sum_{i=1}^{N}a_i u_i.
$$

Substitution shows that the data estimate has the correct expectation for a
uniform batch at fixed $w$:

$$
\mathbb{E}[\widehat g_B]=\nabla L(w).
$$

The formula also handles the shorter final batch. For a full batch,
$b=N$ and $A_B=A$, so $s_B=1$ and the exact full gradient is recovered.

## Adam update

One optimizer step occurs per mini-batch. Let $g_t=\widehat g_B$ be the
gradient at step $t$, and initialize first and second moments with
$m_0=v_0=0$. Adam computes, component by component,

$$
m_t=\beta_1m_{t-1}+(1-\beta_1)g_t,
$$

$$
v_t=\beta_2v_{t-1}+(1-\beta_2)g_t^2.
$$

Because both moving averages start at zero, they are bias-corrected:

$$
\widehat m_t=\frac{m_t}{1-\beta_1^t},
\qquad
\widehat v_t=\frac{v_t}{1-\beta_2^t}.
$$

The weight update is

$$
\boxed{
w_t=w_{t-1}
-\eta\frac{\widehat m_t}{\sqrt{\widehat v_t}+\epsilon}
}
$$

with learning rate $\eta>0$ and numerical stabilizer $\epsilon>0$. Squares,
square roots, divisions, and updates are element-wise. The implementation
tracks $\beta_1^t$ and $\beta_2^t$ by multiplying one more beta after every
batch.

Default optimizer values are:

| Option | Default |
|---|---:|
| Epochs | `100` |
| Batch size | `64` |
| Learning rate $\eta$ | `0.01` |
| L2 coefficient $\lambda$ | `0.0001` |
| $\beta_1$ | `0.9` |
| $\beta_2$ | `0.999` |
| $\epsilon$ | `1e-8` |
| Shuffle | enabled |
| Shuffle seed | `0` |
| Early-stopping patience | `0` (disabled) |
| Minimum improvement | `0` |
| Restore best weights | enabled |

For a fixed input order, options, seed, and implementation, training is
deterministic.

## Epochs, validation, and early stopping

Before updating anything, the trainer validates every sample and initial
weight, then records epoch-zero training and validation metrics. Each epoch:

1. shuffles training indices if requested;
2. updates weights once per mini-batch;
3. evaluates the complete training set;
4. evaluates the complete validation set, if supplied; and
5. updates or stops the validation checkpoint state.

The initial weights are a valid validation checkpoint with epoch number zero.
Let $V_\text{best}$ be its validation objective and let $\delta$ be
`early_stopping_min_delta`. Epoch $e$ is an improvement only when

$$
V_e < V_\text{best}-\delta.
$$

An improvement stores the weights and resets the no-improvement counter.
Otherwise the counter increases. If validation data exists, patience $P$ is
nonzero, and the counter reaches $P$, training stops. With
`restore_best_weights=true`, the selected checkpoint is restored after the
last epoch whether or not early stopping fired. Consequently, the final
reported metrics describe the returned weights, not necessarily the final
entry in the epoch history.

With no validation set, early stopping and checkpoint restoration are not
applied; `best_epoch` is the last completed epoch. Validation comparisons use
the same regularized objective $L$ and the same $\lambda$ as training.

## Initialization and practical data construction

The built-in heuristic weights are a useful warm start. Zero initialization is
useful when testing whether the dataset alone contains enough signal to
recover sensible coefficients. Adam maintains independent moment state for
each `fit()` call; moments are not serialized with weight checkpoints.

Sample weights do not create independent information. When many consecutive
positions come from one game, they are strongly correlated. A dataset producer
can reduce each position's $a_i$, sample fewer plies, or weight each game to a
fixed total contribution.

Split training and validation data by whole game before extracting positions.
A random position-level split leaks nearly identical positions from one game
into both sets and produces an overly optimistic validation loss. Apply the
same sampling and weighting policy to both splits.

The training loop does not standardize features. Adam adapts step sizes per
weight, but extremely different feature scales can still affect saturation
and conditioning. Inspect feature distributions, start with a conservative
learning rate, and use validation performance rather than training loss alone
when selecting hyperparameters.

```cpp
linear_training_options options;
options.epochs = 200;
options.batch_size = 128;
options.learning_rate = 0.003;
options.l2 = 1e-4;
options.early_stopping_patience = 10;

auto weights = linear_engine::default_weights();
linear_training_report report = linear_trainer(options).fit(
    training_samples, weights, validation_samples);

linear_engine engine(io, seed, rollout_limit, weights);
```

`report` contains initial and final MSE, MAE, regularized objective values,
per-epoch metrics, the selected epoch, and whether early stopping fired.

## Persisted data and command-line training

`write_linear_training_samples()` and `read_linear_training_samples()` store
precomputed examples. `write_linear_weights()` and `read_linear_weights()` do
the same for checkpoints. Both text formats include:

- a format identifier;
- `linear_engine::feature_schema_version`;
- the expected feature count; and
- explicit row indices.

Loading fails on a version mismatch, missing value, reordered row, non-finite
number, invalid target, or invalid bias feature. When a feature's order or
meaning changes, increment `feature_schema_version` before producing new data.

The sample format is whitespace-delimited. Its header and rows are

```text
5dchess-linear-samples <schema-version> <D> <N>
<sample-index> <y_i> <a_i> <x_i0> ... <x_i,D-1>
```

The weight format is

```text
5dchess-linear-weights <schema-version> <D> 1
<weight-index> <weight-value>
```

There are exactly `D` indexed weight rows. Floating-point output uses enough
decimal digits to reconstruct every stored `float` exactly.

After a dataset producer has walked completed games, converted each result
with `from_position()`, and written the samples, train a checkpoint with:

```sh
5dtools train-linear \
  --validation validation.data \
  --epochs 200 --batch-size 128 --learning-rate 0.003 \
  --patience 10 \
  training.data linear.weights
```

The command starts with `linear_engine::default_weights()`. Pass `--zero-init`
or `--initial-weights <checkpoint>` to change initialization. It prints the
initial and final train/validation metrics and writes the selected checkpoint.
Run it in the engine with:

```sh
5dchess linear --weights linear.weights
```

Use `5dtools train-linear --help` for every optimizer option.

## Correctness checks

`test/test_linear_training.cpp` guards the mathematical and integration
contracts with:

- central finite differences against the analytic gradient,
  $[L(w+\varepsilon e_j)-L(w-\varepsilon e_j)]/(2\varepsilon)$;
- an explicit check that L2 excludes the bias;
- White/Black target conversion and runtime-evaluation equivalence;
- recovery of known synthetic weights through Adam;
- preservation of sample weighting under one-example mini-batches;
- best-checkpoint restoration under early stopping; and
- exact persistence round trips plus incompatible-schema rejection.

These tests should be updated alongside any change to the objective, feature
perspective, optimizer, or serialized schema.

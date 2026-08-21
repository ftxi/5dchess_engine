# Frozen Linear profile

`linear-trained` preserves the first 64 coefficients of the final
distribution-matched `r=40` training checkpoint produced in August 2026. The
retained schema contains only bias, material, timeline, and move-space
features. Training code, datasets, checkpoint I/O, royal-safety features, and
experiment telemetry are deliberately not part of the engine.

The original 84-feature checkpoint did not demonstrate an advantage over the
hand-written profile in the final direct match: it scored 17 wins, 20 losses,
and 7 draws. Both original Linear profiles nevertheless outperformed MCTS
`r=120` in separate one-second matches.

The embedded 64-feature vector is a projection, not a retrained or independently
benchmarked model. `linear` therefore remains the recommended default;
`linear-trained` exists as a reproducible experimental comparison point.

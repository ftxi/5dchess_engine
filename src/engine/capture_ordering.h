#ifndef CAPTURE_ORDERING_H
#define CAPTURE_ORDERING_H

#include <optional>
#include <stop_token>
#include <vector>

#include "hypercuboid.h"

inline constexpr float capture_ordering_score = 480.0f;

// Cheaply classify captures without applying the move or calculating the
// complete move metadata used by the legacy weighting profile.
bool is_capture_for_weighting(const state &s, full_move move);

float capture_semimove_score(
    const state &s,
    const semimove &move,
    float capture_score = capture_ordering_score);

std::optional<std::vector<std::vector<float>>> capture_coordinate_scores(
    const HC_info &info,
    std::stop_token stop = {},
    float capture_score = capture_ordering_score);

#endif /* CAPTURE_ORDERING_H */


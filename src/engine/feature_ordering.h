#ifndef FEATURE_ORDERING_H
#define FEATURE_ORDERING_H

#include <optional>
#include <stop_token>
#include <vector>

#include "move_info_evaluation.h"

using feature_score_table = std::vector<std::vector<float>>;

inline constexpr float capture_feature_score = 480.0f;
inline constexpr float check_feature_score = 300.0f;

// Only non-negative feature weights are used for search order. These profiles
// affect which candidate is examined first, not its UCT value or evaluation.
move_info_weights capture_ordering_weights(bool include_checks);

std::optional<feature_score_table> feature_coordinate_scores(
    const HC_info &info, const move_info_weights &weights,
    std::stop_token stop = {});

std::optional<feature_score_table> capture_feature_scores(
    const HC_info &info, std::stop_token stop = {});
std::optional<feature_score_table> capture_check_feature_scores(
    const HC_info &info, std::stop_token stop = {});

#endif

#ifndef TACTICAL_ORDERING_H
#define TACTICAL_ORDERING_H

#include <stop_token>
#include "move_info_evaluation.h"

// Scores order expansion/completion only; it does not change UCT or evaluation.
std::optional<std::vector<std::vector<float>>> capture_check_coordinate_scores(
    const HC_info& info, std::stop_token stop = {});

#endif

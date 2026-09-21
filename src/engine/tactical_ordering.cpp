#include "tactical_ordering.h"

std::optional<std::vector<std::vector<float>>> capture_check_coordinate_scores(
    const HC_info& info, std::stop_token stop)
{
    if (stop.stop_requested()) return std::nullopt;
    hc_move_evaluation evaluator(info);
    std::vector<std::vector<float>> result(info.universe.dimension());
    for (index_t axis = 0; axis < info.universe.dimension(); ++axis) {
        auto& scores = result[axis];
        for (index_t coordinate : info.universe[axis]) {
            if (stop.stop_requested()) return std::nullopt;
            // Coordinate IDs can have holes after invalid arrivals are pruned.
            if (scores.size() <= coordinate) scores.resize(static_cast<std::size_t>(coordinate)+1);
            const auto features = evaluator.features(axis,coordinate);
            float score = features[move_info_weights::CHECK] * 300.0f;
            for(std::size_t feature = move_info_weights::QUEEN_CAPTURE;
                feature <= move_info_weights::PAWN_CAPTURE; ++feature)
            {
                if(features[feature] != 0.0f)
                {
                    score += 480.0f;
                    break;
                }
            }
            scores[coordinate] = score;
        }
    }
    return result;
}

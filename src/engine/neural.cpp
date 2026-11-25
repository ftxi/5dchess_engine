#include "neural.h"

#include "magic.h"
#include "move_evaluation.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <locale>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
constexpr auto royal_ray_masks = [] {
    std::array<std::array<bitboard_t, 8>, BOARD_SIZE> masks{};
    constexpr std::array<std::pair<int, int>, 8> directions{{
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}
    }};
    for(int square = 0; square < BOARD_SIZE; ++square)
    {
        for(std::size_t i = 0; i < directions.size(); ++i)
        {
            const auto [dx, dy] = directions[i];
            for(int x = square % BOARD_LENGTH + dx, y = square / BOARD_LENGTH + dy;
                0 <= x && x < BOARD_LENGTH && 0 <= y && y < BOARD_LENGTH; x += dx, y += dy)
                masks[square][i] |= bitboard_t{1} << ppos(x, y);
        }
    }
    return masks;
}();

void append_royal_rays(const state &position, neural_model::ray_features_t &features)
{
    std::array<std::array<int, 10>, 2> worst{};
    const auto [mandatory, optional, unplayable] = position.get_timeline_status();
    for(const auto *lines : {&mandatory, &optional})
    for(int line : *lines)
    {
        const auto [turn, endpoint_color] = position.get_timeline_end(line).get();
        const auto &board = *position.get_board_ptr(line, turn, endpoint_color);
        for(bool defender : {false, true})
        {
            const auto friendly = (defender ? board.black() : board.white()) & ~board.wall();
            for(int royal : marked_pos(board.royal() & friendly))
            {
                const auto occupied = board.occupied();
                const std::array attacks{rook_attack(royal, occupied), bishop_attack(royal, occupied)};
                for(std::size_t kind = 0; kind < attacks.size(); ++kind)
                {
                    const auto open = attacks[kind] & ~occupied;
                    const auto blockers = attacks[kind] & friendly;
                    int undefended = 0, open_without_blocker = 0;
                    int blocker_cost = 0;
                    for(int square : marked_pos(blockers))
                    {
                        undefended += !(board.attacks_to(square) & friendly & ~pmask(royal));
                        // An adjacent blocker leaves no landing square on its ray.
                        if(!(king_attack(royal) & pmask(square)))
                            blocker_cost += (board.royal() & pmask(square))
                                ? 2000 : static_cast<int>(capture_piece_value(board.get_piece(square)));
                    }
                    for(std::size_t ray = 4 * kind; ray < 4 * (kind + 1); ++ray)
                    {
                        const auto mask = royal_ray_masks[royal][ray];
                        open_without_blocker += (open & mask) && !(blockers & mask);
                    }
                    const std::array scores{
                        std::popcount(open), std::popcount(blockers),
                        undefended, open_without_blocker, blocker_cost
                    };
                    for(std::size_t i = 0; i < scores.size(); ++i)
                    {
                        auto &peak = worst[defender][kind * scores.size() + i];
                        peak = std::max(peak, scores[i]);
                    }
                }
            }
        }
    }
    const bool mover = position.get_present().c();
    for(std::size_t i = 0; i < worst[mover].size(); ++i)
        features[neural_model::input_count + i] = (worst[!mover][i] - worst[mover][i])
            / (i % 5 == 4 ? 100.0f : 1.0f);
}

neural_model::ray_features_t add_royal_rays(
    const state &position, const neural_model::features_t &base)
{
    neural_model::ray_features_t result;
    std::copy(base.begin(), base.end(), result.begin());
    append_royal_rays(position, result);
    return result;
}

neural_model::protection_features_t add_protection(
    const state &position, const neural_model::ray_features_t &base)
{
    neural_model::protection_features_t result;
    std::copy(base.begin(), base.end(), result.begin());
    std::array<int, 3> balance{};
    const bool mover = position.get_present().c();
    const auto [mandatory, optional, unplayable] = position.get_timeline_status();
    for(const auto *lines : {&mandatory, &optional})
    for(int line : *lines)
    {
        const auto [turn, color] = position.get_timeline_end(line).get();
        const auto &board = *position.get_board_ptr(line, turn, color);
        const auto strong = board.lpawn() | board.common_king();
        const auto medium = board.bishop() | board.knight() | board.rook();
        const auto weak = board.princess() | board.queen();
        const auto white = board.white() & ~board.wall();
        const auto black = board.black() & ~board.wall();
        for(int target : marked_pos((white | black) & ~board.royal()))
        {
            const bool defender = (black & pmask(target)) != 0;
            const auto attacks = board.attacks_to(target);
            if(!(attacks & (defender ? white : black))) continue;
            const auto defenders = attacks & (defender ? black : white);
            const int sign = defender == mover ? 1 : -1;
            if(defenders & strong) balance[0] += sign;
            else if(defenders & medium) balance[1] += sign;
            else if(defenders & weak) balance[2] += sign;
        }
    }
    std::copy(balance.begin(), balance.end(), result.begin() + neural_model::ray_input_count);
    return result;
}

template<bool IncludeCaptureGain>
std::optional<std::array<float, 4>> summarize_candidates(
    const HC_info &info, std::stop_token stop)
{
    const auto &position = info.get_state();
    const bool player = position.get_present().c();
    std::array<float, 2> best_gain{};
    std::size_t physical = 0;
    std::vector<ext_move> jumps;
    integer_set seen_branch_coordinates;
    for(index_t axis = 0; axis < info.dimension(); ++axis)
    {
        for(index_t coordinate : info.get_universe()[axis])
        {
            if(stop.stop_requested()) return std::nullopt;
            // Branch axes copy the same arrival table, but check pruning can
            // leave different subsets on each. Visit the first surviving copy.
            if(axis > info.first_branch_axis() && seen_branch_coordinates.contains(coordinate))
                continue;
            const auto boards = info.get_move_boards(axis, coordinate);
            if(!boards) continue;
            if constexpr(IncludeCaptureGain)
            {
                const auto victim = to_white(piece_name(boards->move.captured_piece(position)));
                auto &best = best_gain[boards->departure ? 1 : 0];
                const float value = capture_piece_value(victim);
                if(value > best && victim != KING_W && victim != ROYAL_QUEEN_W)
                {
                    const float loss = solid_capture_exposure_loss(
                        *boards->result, boards->move.to.xy(), player);
                    best = std::max(best, value - loss);
                }
            }
            if(!(has_physical_check(*boards->result, !player)
                || (boards->departure && has_physical_check(*boards->departure, !player))))
                continue;
            if(boards->departure)
                jumps.emplace_back(boards->move, position);
            else
                ++physical; // The generator stores each physical move once, with its default promotion.
        }
        if(axis >= info.first_branch_axis() && axis + 1 < info.dimension())
            seen_branch_coordinates |= info.get_universe()[axis];
    }
    // A jump to a playable endpoint may occur as both a branch and an
    // arrival on the existing timeline.
    std::sort(jumps.begin(), jumps.end());
    const auto end = std::unique(jumps.begin(), jumps.end());
    return std::array{static_cast<float>(physical), static_cast<float>(end - jumps.begin()),
        best_gain[0] / 100.0f, best_gain[1] / 100.0f};
}

template<bool IncludeCaptureGain>
using candidate_features_t = std::conditional_t<IncludeCaptureGain,
    neural_model::capture_gain_features_t, neural_model::local_check_features_t>;

template<bool IncludeCaptureGain>
std::optional<candidate_features_t<IncludeCaptureGain>> add_candidate_features(
    const HC_info &info,
    const neural_model::protection_features_t &base, std::stop_token stop = {})
{
    const auto summary = summarize_candidates<IncludeCaptureGain>(info, stop);
    if(!summary || stop.stop_requested()) return std::nullopt;
    candidate_features_t<IncludeCaptureGain> result;
    std::copy(base.begin(), base.end(), result.begin());
    std::copy_n(summary->begin(), result.size() - base.size(), result.begin() + base.size());
    return result;
}

neural_model::features_t solid_features(const state &position, const HC &domain, index_t first_branch)
{
    // Preserve the non-volume features and the legacy linear schema.
    auto features = linear_cutoff_evaluation::extract_features(position, {});
    auto log_volume = [&](index_t begin, index_t end) {
        double sum = 0;
        for(index_t axis = begin; axis < end; ++axis)
        {
            const auto size = domain[axis].size();
            // log(max(1, volume)): empty and unit candidate factors map to zero.
            // In particular, an empty pruned axis must never produce -infinity.
            if(size == 0) return 0.0f;
            sum += std::log(static_cast<double>(size));
        }
        return static_cast<float>(sum);
    };
    features[neural_model::log_playable_volume_offset] = log_volume(0, first_branch);
    features[neural_model::log_new_volume_offset] = log_volume(first_branch, domain.dimension());
    return features;
}

template<bool IncludeCaptureGain>
candidate_features_t<IncludeCaptureGain> extract_candidate_features(const state &position)
{
    std::optional<HC> pruned;
    const auto info = HC_info::build_info(position, {.remove_check = true}, &pruned);
    const auto &domain = pruned ? *pruned : info.get_universe();
    const auto solid = solid_features(position, domain, info.first_branch_axis());
    const auto protection = add_protection(position, add_royal_rays(position, solid));
    return add_candidate_features<IncludeCaptureGain>(info, protection).value();
}
} /* anonymous namespace */

neural_model neural_model::load(const std::filesystem::path &path)
{
    std::ifstream input(path);
    input.imbue(std::locale::classic());
    std::string magic, schema;
    int version = 0, hidden1 = 0, hidden2 = 0, outputs = 0;
    std::size_t inputs = 0;
    if(!(input >> magic >> version >> schema >> inputs >> hidden1 >> hidden2 >> outputs)
        || magic != "5DNEURAL" || version != 1 || hidden1 != 32 || hidden2 != 16 || outputs != 1)
        throw std::invalid_argument("invalid neural model header or dimensions");
    if(!((inputs == input_count && (schema == "linear70-v1-mover" || schema == solid_schema))
        || (inputs == ray_input_count && schema == ray_schema)
        || (inputs == protection_input_count && schema == protection_schema)
        || (inputs == local_check_input_count && schema == local_check_schema)
        || (inputs == capture_gain_input_count && schema == capture_gain_schema)))
        throw std::invalid_argument("invalid neural model header or dimensions");
    neural_model model;
    model.uses_solid_features = schema != "linear70-v1-mover";
    model.model_inputs = inputs;
    auto read = [&](std::span<float> values) {
        for(auto &value : values)
            if(!(input >> value) || !std::isfinite(value))
                throw std::invalid_argument("invalid or truncated neural model numbers");
    };
    read(std::span(model.mean).first(inputs));
    read(std::span(model.inverse_scale).first(inputs));
    for(float value : model.inverse_scale)
        if(value < 0)
            throw std::invalid_argument("negative neural normalization scale");
    read(std::span(model.weights1).first(inputs * model.bias1.size())); read(model.bias1);
    read(model.weights2); read(model.bias2);
    read(model.weights3);
    std::array<float, 1> bias;
    read(bias);
    model.bias3 = bias[0];
    std::string extra;
    if(input >> extra) throw std::invalid_argument("trailing neural model data");
    return model;
}

neural_model::features_t neural_model::extract_solid_features(const state &position)
{
    const auto info = HC_info::build_info(position,
        {.remove_check = true, .prune_optional_moves = true});
    return extract_solid_features(position, info);
}

neural_model::features_t neural_model::extract_solid_features(
    const state &position, const HC_info &info)
{
    return solid_features(position, info.get_universe(), info.first_branch_axis());
}

neural_model::features_t neural_model::extract_features(
    const state &position, const move_space_data &legacy_space) const
{
    if(uses_solid_features) return extract_solid_features(position);
    return linear_cutoff_evaluation::extract_features(position, legacy_space);
}

neural_model::ray_features_t neural_model::extract_ray_features(const state &position)
{
    return add_royal_rays(position, extract_solid_features(position));
}

neural_model::protection_features_t neural_model::extract_protection_features(const state &position)
{
    return add_protection(position, extract_ray_features(position));
}

neural_model::local_check_features_t neural_model::extract_local_check_features(const state &position)
{
    return extract_candidate_features<false>(position);
}

neural_model::capture_gain_features_t neural_model::extract_capture_gain_features(const state &position)
{
    return extract_candidate_features<true>(position);
}

std::optional<reward_t<rollout_details>> neural_model::evaluate_position(
    state position, std::stop_token stop) const
{
    if(stop.stop_requested()) return std::nullopt;
    std::optional<reward_t<rollout_details>> outcome;
    std::optional<float> mover;
    if(uses_solid_features)
    {
        const bool needs_candidates = model_inputs >= local_check_input_count;
        std::optional<HC> pruned;
        const auto info = HC_info::build_info(position,
            {.remove_check = true, .prune_optional_moves = !needs_candidates},
            needs_candidates ? &pruned : nullptr);
        const auto &domain = pruned ? *pruned : info.get_universe();
        if(stop.stop_requested()) return std::nullopt;
        const auto witness = first_position_action(info, info.partition_by_branches(domain), stop);
        if(stop.stop_requested()) return std::nullopt;
        if(witness)
            outcome = rollout_cutoff_evaluation{}.cutoff_reward(position, 0, stop, nullptr);
        else
            // Optional pruning is heuristic: only the complete space can prove
            // that no action exists. It may instead supply a fallback witness.
            outcome = evaluate_zero_position(position, stop);
        if(!outcome || outcome->data.end != rollout_details::termination::ACTION_LIMIT)
            return outcome;
        mover = [&]() -> std::optional<float> {
            const auto solid = solid_features(position, domain, info.first_branch_axis());
            if(model_inputs == input_count) return evaluate_features(solid);
            const auto rays = add_royal_rays(position, solid);
            if(model_inputs == ray_input_count) return evaluate_features(rays);
            const auto protection = add_protection(position, rays);
            if(model_inputs == protection_input_count) return evaluate_features(protection);
            if(model_inputs == local_check_input_count)
            {
                const auto features = add_candidate_features<false>(info, protection, stop);
                if(!features) return std::nullopt;
                return evaluate_features(*features);
            }
            const auto features = add_candidate_features<true>(info, protection, stop);
            if(!features) return std::nullopt;
            return evaluate_features(*features);
        }();
    }
    else
    {
        move_space_data space{};
        outcome = evaluate_zero_position(position, stop, &space);
        if(!outcome || outcome->data.end != rollout_details::termination::ACTION_LIMIT)
            return outcome;
        mover = evaluate_features(extract_features(position, space));
    }
    if(!mover || stop.stop_requested()) return std::nullopt;
    outcome->score = position.get_present().c() ? -*mover : *mover;
    return outcome;
}

float neural_model::evaluate_features(std::span<const float> features) const
{
    if(features.size() != model_inputs)
        throw std::invalid_argument("wrong neural input count for model schema");
    capture_gain_features_t normalized;
    for(std::size_t i = 0; i < features.size(); ++i)
    {
        if(!std::isfinite(features[i]))
            throw std::invalid_argument("nonfinite neural input");
        normalized[i] = (features[i] - mean[i]) * inverse_scale[i];
        if(!std::isfinite(normalized[i]))
            throw std::invalid_argument("nonfinite normalized neural input");
    }
    auto affine = [](const auto &input, const auto &weights, const auto &bias) {
        auto output = bias;
        for(std::size_t row = 0; row < output.size(); ++row)
        {
            float value = bias[row];
            for(std::size_t col = 0; col < input.size(); ++col)
                value += weights[row * input.size() + col] * input[col];
            if(!std::isfinite(value))
                throw std::invalid_argument("nonfinite neural activation");
            output[row] = std::max(0.0f, value);
        }
        return output;
    };
    const auto hidden1 = affine(std::span(normalized).first(model_inputs), weights1, bias1);
    const auto hidden2 = affine(hidden1, weights2, bias2);
    float z = bias3;
    for(std::size_t i = 0; i < hidden2.size(); ++i) z += weights3[i] * hidden2[i];
    if(!std::isfinite(z)) throw std::invalid_argument("nonfinite neural output");
    return std::tanh(z);
}

#ifndef NEURAL_H
#define NEURAL_H

#include <filesystem>
#include <cmath>
#include <span>
#include <string_view>
#include "linear.h"
#include "solid.h"

// Text format: 5DNEURAL 1, schema tag, dimensions, means, inverse scales,
// then row-major weights and biases for each affine layer. All numbers float32.
class neural_model
{
public:
    using features_t = linear_cutoff_evaluation::feature_vector_t;
    static constexpr std::size_t input_count = 70;
    static constexpr std::size_t parameter_count = 2817;
    static constexpr std::size_t ray_input_count = input_count + 10;
    static constexpr std::size_t protection_input_count = ray_input_count + 3;
    static constexpr std::size_t local_check_input_count = protection_input_count + 2;
    static constexpr std::size_t capture_gain_input_count = local_check_input_count + 2;
    using ray_features_t = std::array<float, ray_input_count>;
    using protection_features_t = std::array<float, protection_input_count>;
    using local_check_features_t = std::array<float, local_check_input_count>;
    using capture_gain_features_t = std::array<float, capture_gain_input_count>;
    static constexpr std::string_view solid_schema = "solid70-v2-mover";
    static constexpr std::string_view ray_schema = "solid80-rays-v3-mover";
    static constexpr std::string_view protection_schema = "solid83-protection-v4-mover";
    static constexpr std::string_view local_check_schema = "solid85-local-checks-v5-mover";
    static constexpr std::string_view capture_gain_schema = "solid87-capture-gain-v6-mover";
    static constexpr std::size_t log_playable_volume_offset = 62;
    static constexpr std::size_t log_new_volume_offset = 63;
    static neural_model load(const std::filesystem::path &path);
    float evaluate_features(std::span<const float> features) const;
    // Existing Solid schemas use optional-pruned volumes for inputs 62 and 63.
    static features_t extract_solid_features(const state &position);
    // The caller supplies the feature schema's volume domain in info.get_universe().
    static features_t extract_solid_features(const state &position, const HC_info &info);
    static ray_features_t extract_ray_features(const state &position);
    static protection_features_t extract_protection_features(const state &position);
    static local_check_features_t extract_local_check_features(const state &position);
    static capture_gain_features_t extract_capture_gain_features(const state &position);
    features_t extract_features(const state &position, const move_space_data &legacy_space) const;
    std::optional<reward_t<rollout_details>> evaluate_position(
        state position, std::stop_token stop) const;

private:
    capture_gain_features_t mean{}, inverse_scale{};
    std::array<float, capture_gain_input_count * 32> weights1{};
    std::array<float, 32> bias1{};
    std::array<float, 32 * 16> weights2{};
    std::array<float, 16> bias2{}, weights3{};
    float bias3 = 0;
    bool uses_solid_features = false;
    std::size_t model_inputs = input_count;
};

struct quiescence_stats
{
    unsigned successors = 0, queries = 0, work = 0, deepest = 0;
};

std::optional<reward_t<rollout_details>> evaluate_quiescence(
    const neural_model &model, state position, unsigned depth, std::stop_token stop,
    quiescence_stats &stats);

enum class neural_decay { inverse, square_root, exponential };

struct neural_guidance_options
{
    bool prior_only = true;
    bool cache_evaluations = true;
    float value_scale = 1.0f;
    double strength = 0.3, horizon = 64.0;
    neural_decay decay = neural_decay::inverse;

    void validate() const
    {
        if(!(value_scale >= 0 && value_scale <= 1) || !std::isfinite(value_scale)
            || !(strength >= 0 && strength <= 1) || !std::isfinite(strength)
            || !(horizon >= 0) || !std::isfinite(horizon))
            throw std::invalid_argument("invalid neural scale, strength or horizon");
        if(decay != neural_decay::inverse && decay != neural_decay::square_root
            && decay != neural_decay::exponential)
            throw std::invalid_argument("invalid neural decay");
    }
    float evaluation_scale() const
    {
        return prior_only && (strength == 0 || horizon == 0) ? 0.0f : value_scale;
    }
    double weight(std::size_t visits) const
    {
        if(!prior_only || strength == 0 || horizon == 0) return 0;
        const double x = static_cast<double>(visits) / horizon;
        switch(decay)
        {
        case neural_decay::inverse: return strength / (1+x);
        case neural_decay::square_root: return strength / std::sqrt(1+x);
        case neural_decay::exponential: return strength * std::exp(-x);
        }
        return 0;
    }
};

struct neural_node_data
{
    std::optional<reward_t<rollout_details>> cached_evaluation;
    double prior_sum = 0;
    std::size_t prior_count = 0;
    bool prior_evidence_recorded = false;
};

struct neural_diagnostics
{
    std::size_t cache_hits = 0, cache_misses = 0, prior_choices = 0, changed_choices = 0;
    std::size_t unique_updates = 0, repeated_skips = 0;
};

struct neural_observer : mcts_observer
{
    std::shared_ptr<neural_diagnostics> diagnostics;
    explicit neural_observer(std::shared_ptr<neural_diagnostics> diagnostics_ = nullptr)
        : diagnostics(std::move(diagnostics_)) {}
    void reset()
    {
        mcts_observer::reset();
        if(diagnostics) *diagnostics = {};
    }
    std::string report() const
    {
        std::ostringstream out;
        out << mcts_observer::report();
        if(diagnostics)
            out << "\nneural_stats cache_hits=" << diagnostics->cache_hits
                << " cache_misses=" << diagnostics->cache_misses
                << " prior_choices=" << diagnostics->prior_choices
                << " changed_choices=" << diagnostics->changed_choices
                << " unique_updates=" << diagnostics->unique_updates
                << " repeated_skips=" << diagnostics->repeated_skips;
        return out.str();
    }
};

namespace neural_detail
{
inline std::optional<reward_t<rollout_details>> evaluate_position(
    const neural_model *model, float scale, state position, std::stop_token stop, unsigned depth = 0)
{
    if(!model || scale == 0.0f) return evaluate_zero_position(std::move(position), stop);
    quiescence_stats stats;
    auto result = depth ? evaluate_quiescence(*model, std::move(position), depth, stop, stats)
                        : model->evaluate_position(std::move(position), stop);
    if(result && result->data.end == rollout_details::termination::ACTION_LIMIT)
        result->score *= scale;
    return result;
}

template<class Node, class Diagnostics>
std::optional<reward_t<rollout_details>> evaluate_ceiling(
    Node &node, const neural_model *model, float scale, bool use_cache,
    std::stop_token stop, Diagnostics *diagnostics, unsigned depth = 0)
{
    if(stop.stop_requested()) return std::nullopt;
    auto &cached = node.get_info().tree_policy_data.cached_evaluation;
    if(use_cache && cached)
    {
        search_trace_phase phase("evaluation_cache", &node.get_hc_info().get_state());
        if(diagnostics) ++diagnostics->cache_hits;
        return cached;
    }
    if(diagnostics) ++diagnostics->cache_misses;
    state position = [&] {
        search_trace_phase phase("state_at_ceiling", &node.get_hc_info().get_state());
        return detail::state_at_ceiling(node);
    }();
    search_trace_phase phase("evaluation", &position);
    auto result = evaluate_position(model, scale, std::move(position), stop, depth);
    if(use_cache && result && !stop.stop_requested()) cached = result;
    return result;
}
} /* namespace neural_detail */

class neural_leaf_policy
{
    std::shared_ptr<const neural_model> model;
    neural_guidance_options options;
    std::shared_ptr<neural_diagnostics> diagnostics;
public:
    using result_type = reward_t<rollout_details>;
    explicit neural_leaf_policy(std::shared_ptr<const neural_model> model,
        neural_guidance_options options_ = {}, std::shared_ptr<neural_diagnostics> diagnostics_ = nullptr)
        : model(std::move(model)), options(options_), diagnostics(std::move(diagnostics_))
    {
        if(!this->model) throw std::invalid_argument("missing neural model");
        options.validate();
    }
    template<class Observer>
    std::optional<result_type> evaluate(state position, std::stop_token stop, Observer &)
    {
        return neural_detail::evaluate_position(model.get(), options.evaluation_scale(),
            std::move(position), stop);
    }

    template<class Node, class Observer>
    std::optional<result_type> evaluate_ceiling(Node &node, std::stop_token stop, Observer &)
    {
        return neural_detail::evaluate_ceiling(node, model.get(), options.evaluation_scale(),
            options.cache_evaluations, stop, diagnostics.get());
    }
};

struct neural_child_selection
{
    neural_guidance_options options;
    std::shared_ptr<neural_diagnostics> diagnostics;
    solid_child_selection exploration;
    template<class Node>
    Node *operator()(Node *node) const
    {
        if(!options.prior_only || options.evaluation_scale() == 0)
            return exploration(node);
        const auto &parent = node->get_info();
        const auto &prior = parent.tree_policy_data;
        const double center = prior.prior_count ? prior.prior_sum / prior.prior_count : 0;
        auto *best = exploration(node,
            [&](const Node *child) {
                const auto &info = child->get_info();
                const auto &data = info.tree_policy_data;
                return data.prior_count ? options.weight(info.visits)
                    * (data.prior_sum / data.prior_count - center) : 0.0;
            });
        if(diagnostics)
        {
            ++diagnostics->prior_choices;
            diagnostics->changed_choices += best != exploration(node);
        }
        return best;
    }
};

struct neural_backpropagation
{
    neural_guidance_options options;
    std::shared_ptr<neural_diagnostics> diagnostics;
    template<class Node, class Result, class Observer>
    void backpropagate(Node *node, const Result &reward, Observer &observer)
    {
        if(!options.prior_only)
        {
            sum_backpropagation{}.backpropagate(node, reward, observer);
            return;
        }
        const bool nonterminal = reward.data.end == rollout_details::termination::ACTION_LIMIT;
        auto direct = reward;
        if(nonterminal) direct.score = 0;
        sum_backpropagation{}.backpropagate(node, direct, observer);
        auto &leaf = node->get_info().tree_policy_data;
        if(!nonterminal || options.evaluation_scale() == 0) return;
        if(leaf.prior_evidence_recorded)
        {
            if(diagnostics) ++diagnostics->repeated_skips;
            return;
        }
        leaf.prior_evidence_recorded = true;
        if(diagnostics) ++diagnostics->unique_updates;
        // One deterministic completed leaf contributes once, across all its
        // ancestor choices. Repeated visits still update ordinary UCT above.
        for(auto *ancestor = node; ancestor; ancestor = ancestor->get_parent())
        {
            auto &data = ancestor->get_info().tree_policy_data;
            data.prior_sum += reward.score;
            ++data.prior_count;
        }
    }
};

using neural_tree_policy = progressive_widening_tree_policy<
    solid_HC_ordering, neural_child_selection, neural_node_data>;

using neural_pw_mcts_engine = basic_mcts_engine<
    neural_tree_policy, neural_leaf_policy,
    neural_backpropagation, most_visited_selection, neural_observer>;

class neural_pw_engine final : public neural_pw_mcts_engine
{
    neural_pw_engine(std::unique_ptr<io_handler> io,
        std::optional<std::uint32_t> seed, double constant, double alpha,
        std::shared_ptr<const neural_model> model, neural_guidance_options options,
        std::size_t prefix_budget, bool weighted_exploration, bool prune_optional_moves,
        std::shared_ptr<neural_diagnostics> diagnostics)
        : neural_pw_mcts_engine(neural_leaf_policy{std::move(model), options, diagnostics},
            neural_tree_policy{seed, constant, alpha,
                solid_scores, neural_child_selection{options, diagnostics, {weighted_exploration}},
                HC_build_options{.remove_check = true, .prune_optional_moves = prune_optional_moves},
                prefix_budget},
            neural_backpropagation{options, diagnostics}, {}, neural_observer{diagnostics}, std::move(io)) {}
public:
    neural_pw_engine(std::unique_ptr<io_handler> io,
        std::optional<std::uint32_t> seed, double constant, double alpha,
        std::shared_ptr<const neural_model> model, neural_guidance_options options = {},
        std::size_t prefix_budget = default_fine_prefix_budget,
        bool weighted_exploration = true, bool prune_optional_moves = false)
        : neural_pw_engine(std::move(io), seed, constant, alpha, std::move(model), options, prefix_budget,
            weighted_exploration, prune_optional_moves, std::make_shared<neural_diagnostics>()) {}
};

static_assert(neural_model::input_count == linear_cutoff_evaluation::features_count);
static_assert(DefaultPolicy<neural_leaf_policy, mcts_observer>);
#endif /* NEURAL_H */

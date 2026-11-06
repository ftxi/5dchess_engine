#ifndef NEURAL_H
#define NEURAL_H

#include <filesystem>
#include <cmath>
#include <string_view>
#include "linear.h"

// Text format: 5DNEURAL 1, schema tag, dimensions, means, inverse scales,
// then row-major weights and biases for each affine layer. All numbers float32.
class neural_model
{
public:
    using features_t = linear_cutoff_evaluation::feature_vector_t;
    static constexpr std::size_t input_count = 70;
    static constexpr std::size_t parameter_count = 2817;
    static constexpr std::string_view solid_schema = "solid70-v2-mover";
    static constexpr std::size_t log_playable_volume_offset = 62;
    static constexpr std::size_t log_new_volume_offset = 63;
    static neural_model load(const std::filesystem::path &path);
    float evaluate_features(const features_t &features) const;
    // The export and inference paths share the same solid-pruned construction.
    static features_t extract_solid_features(const state &position);
    static features_t extract_solid_features(const state &position, const HC_info &info);
    features_t extract_features(const state &position, const move_space_data &legacy_space) const;
    std::optional<reward_t<rollout_details>> evaluate_position(
        state position, std::stop_token stop) const;

private:
    features_t mean{}, inverse_scale{};
    std::array<float, 70 * 32> weights1{};
    std::array<float, 32> bias1{};
    std::array<float, 32 * 16> weights2{};
    std::array<float, 16> bias2{}, weights3{};
    float bias3 = 0;
    bool uses_solid_features = false;
};

enum class neural_decay { inverse, square_root, exponential };

struct neural_guidance_options
{
    bool prior_only = false;
    bool cache_evaluations = true;
    float value_scale = 1.0f;
    double strength = 0.3, horizon = 32.0;
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
        if(options.value_scale == 0) return evaluate_zero_position(std::move(position), stop);
        auto outcome = model->evaluate_position(std::move(position), stop);
        if(!outcome || outcome->data.end != rollout_details::termination::ACTION_LIMIT)
            return outcome;
        outcome->score *= options.value_scale;
        return outcome;
    }

    template<class Node, class Observer>
    std::optional<result_type> evaluate_ceiling(Node &node, std::stop_token stop, Observer &observer)
    {
        if(stop.stop_requested()) return std::nullopt;
        auto &cached = node.get_info().tree_policy_data.cached_evaluation;
        if(options.cache_evaluations && cached)
        {
            if(diagnostics) ++diagnostics->cache_hits;
            return cached;
        }
        if(diagnostics) ++diagnostics->cache_misses;
        auto result = evaluate(detail::state_at_ceiling(node), stop, observer);
        if(options.cache_evaluations && result) cached = result;
        return result;
    }
};

struct neural_child_selection
{
    neural_guidance_options options;
    std::shared_ptr<neural_diagnostics> diagnostics;
    template<class Node>
    Node *operator()(Node *node) const
    {
        if(!options.prior_only || options.strength == 0 || options.horizon == 0)
            return uct_detail::best_uct_child(node);
        const auto &parent = node->get_info();
        const auto &prior = parent.tree_policy_data;
        const double center = prior.prior_count ? prior.prior_sum / prior.prior_count : 0;
        Node *best = nullptr;
        const bool maximizing = !node->get_player();
        double best_score = maximizing ? -std::numeric_limits<double>::infinity()
                                       : std::numeric_limits<double>::infinity();
        for(auto *child : node->get_children())
        {
            const auto &info = child->get_info();
            if(!info.registered || info.visits == 0) continue;
            double score = uct(info.sum_reward, info.visits, parent.visits, maximizing);
            const auto &data = info.tree_policy_data;
            if(data.prior_count)
                score += options.weight(info.visits)
                    * (data.prior_sum / data.prior_count - center);
            if(!best || (maximizing ? score > best_score : score < best_score))
            {
                best = child;
                best_score = score;
            }
        }
        if(diagnostics)
        {
            ++diagnostics->prior_choices;
            diagnostics->changed_choices += best != uct_detail::best_uct_child(node);
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
        if(!nonterminal) return;
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
    scored_HC_ordering, neural_child_selection, neural_node_data>;

using neural_pw_mcts_engine = basic_mcts_engine<
    neural_tree_policy, neural_leaf_policy,
    neural_backpropagation, most_visited_selection, neural_observer>;

class neural_pw_engine final : public neural_pw_mcts_engine
{
    neural_pw_engine(std::unique_ptr<io_handler> io,
        std::optional<std::uint32_t> seed, double constant, double alpha,
        std::shared_ptr<const neural_model> model, neural_guidance_options options,
        std::shared_ptr<neural_diagnostics> diagnostics)
        : neural_pw_mcts_engine(neural_leaf_policy{std::move(model), options, diagnostics},
            neural_tree_policy{seed, constant, alpha,
                [](const HC_info &info, std::stop_token stop) {
                    return move_evaluator(info, capture_check_weights,
                        capture_ordering::mvvlva_exposure).build_score_table(stop);
                }, neural_child_selection{options, diagnostics},
                HC_build_options{.remove_check = true, .prune_optional_moves = true}},
            neural_backpropagation{options, diagnostics}, {}, neural_observer{diagnostics}, std::move(io)) {}
public:
    neural_pw_engine(std::unique_ptr<io_handler> io,
        std::optional<std::uint32_t> seed, double constant, double alpha,
        std::shared_ptr<const neural_model> model, neural_guidance_options options = {})
        : neural_pw_engine(std::move(io), seed, constant, alpha, std::move(model), options,
            std::make_shared<neural_diagnostics>()) {}
};

static_assert(neural_model::input_count == linear_cutoff_evaluation::features_count);
static_assert(DefaultPolicy<neural_leaf_policy, mcts_observer>);
#endif /* NEURAL_H */

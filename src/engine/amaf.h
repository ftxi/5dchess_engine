#ifndef AMAF_H
#define AMAF_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "neural.h"
#include "solid.h"

enum class amaf_decay { inverse, square_root, exponential };
enum class amaf_credit { blend, centered, outside };
enum class amaf_application { selection, admission, both, children };

struct amaf_options
{
    double strength = 1.0;
    double horizon = 32.0;
    amaf_decay decay = amaf_decay::inverse;
    // In prior mode, learned nonterminal values guide action-local AMAF only;
    // ordinary UCT statistics retain zero cutoff and exact terminal rewards.
    bool neural_prior_only = false;
    amaf_credit credit = amaf_credit::blend;
    amaf_application application = amaf_application::selection;
    bool unique_leaf_evidence = false;

    void validate() const
    {
        if(!(strength >= 0.0 && strength <= 1.0) || !std::isfinite(strength))
            throw std::invalid_argument("AMAF strength must be in [0, 1]");
        if(!(horizon >= 0.0) || !std::isfinite(horizon))
            throw std::invalid_argument("AMAF horizon must be nonnegative and finite");
        if(decay != amaf_decay::inverse && decay != amaf_decay::square_root
            && decay != amaf_decay::exponential)
            throw std::invalid_argument("unknown AMAF decay");
        if(credit != amaf_credit::blend && credit != amaf_credit::centered
            && credit != amaf_credit::outside)
            throw std::invalid_argument("unknown AMAF credit");
        if(application != amaf_application::selection && application != amaf_application::admission
            && application != amaf_application::both && application != amaf_application::children)
            throw std::invalid_argument("unknown AMAF application");
        if(unique_leaf_evidence && application != amaf_application::admission
            && application != amaf_application::children)
            throw std::invalid_argument("unique-leaf AMAF evidence requires admission or children mode");
    }

    bool enabled() const { return strength > 0.0 && horizon > 0.0; }

    double weight(std::size_t direct_visits) const
    {
        if(!enabled()) return 0.0;
        const double ratio = static_cast<double>(direct_visits) / horizon;
        switch(decay)
        {
        case amaf_decay::inverse: return strength / (1.0 + ratio);
        case amaf_decay::square_root: return strength / std::sqrt(1.0 + ratio);
        case amaf_decay::exponential: return strength * std::exp(-ratio);
        }
        return 0.0;
    }
};

struct amaf_diagnostics
{
    std::size_t lookups = 0, shared_uses = 0, changed_values = 0;
    std::size_t coordinate_updates = 0, action_updates = 0;
    std::size_t nonzero_coordinate_updates = 0, tables = 0;
    std::size_t ordering_rebuilds = 0, ordering_changed_axes = 0;
    std::size_t cache_hits = 0, cache_misses = 0;
    std::size_t child_choices = 0, changed_child_choices = 0;
    std::size_t unique_evidence_updates = 0, repeated_evidence_skips = 0;
};

namespace amaf_detail
{
struct coordinate
{
    index_t axis, entry;
    bool operator==(const coordinate &) const = default;
};

struct coordinate_hash
{
    std::size_t operator()(const coordinate &key) const noexcept
    {
        const auto first = std::hash<index_t>{}(key.axis);
        const auto second = std::hash<index_t>{}(key.entry);
        return first ^ (second + std::size_t{0x9e3779b9} + (first << 6) + (first >> 2));
    }
};

struct statistic
{
    double sum_reward = 0.0;
    std::size_t visits = 0;
};

struct table
{
    // Coordinates belong only to this nodal action root's HC context.
    std::unordered_map<coordinate, statistic, coordinate_hash> entries;
    double sum_reward = 0.0;
    std::size_t visits = 0;
    bool nonzero_seen = false;
};
} /* namespace amaf_detail */


// One cached, context-owned permutation per HC axis. Updating its learned part
// never removes a coordinate or bypasses the normal legality search.
class amaf_HC_ordering
{
    struct data
    {
        std::vector<std::vector<index_t>> base, learned;
        std::vector<std::vector<std::size_t>> base_rank;
        std::vector<std::vector<float>> scores;
        std::vector<std::vector<double>> adjusted;
        std::vector<std::size_t> version;
        const amaf_detail::table *evidence = nullptr;
        amaf_options options;
        bool black = false;
        std::shared_ptr<amaf_diagnostics> diagnostics;
    };
    std::shared_ptr<data> state;
    bool base_only = false;

public:
    amaf_HC_ordering(const HC &universe,
        const std::vector<std::vector<float>> &scores, std::mt19937 &rng)
        : state(std::make_shared<data>())
    {
        const scored_HC_ordering initial(universe, scores, rng);
        state->scores = scores;
        const auto size = universe.dimension();
        state->base.resize(size); state->learned.resize(size);
        state->base_rank.resize(size); state->adjusted.resize(size);
        state->version.assign(size, std::numeric_limits<std::size_t>::max());
        for(index_t n = 0; n < size; ++n)
        {
            for(index_t i : initial(n, universe[n])) state->base[n].push_back(i);
            state->learned[n] = state->base[n];
            state->base_rank[n].resize(scores[n].size());
            state->adjusted[n].resize(scores[n].size());
            for(std::size_t rank = 0; rank < state->base[n].size(); ++rank)
                state->base_rank[n][state->base[n][rank]] = rank;
        }
    }

    float weight(index_t axis, index_t coordinate) const
    {
        return solid_exploration_weight(state->scores[axis][coordinate]);
    }

    void bind(const amaf_detail::table *table, amaf_options options,
        bool black, std::shared_ptr<amaf_diagnostics> diagnostics)
    {
        state->evidence = table; state->options = options;
        state->black = black; state->diagnostics = std::move(diagnostics);
    }

    detail::ordered_coordinate_range operator()(index_t axis, const integer_set &allowed) const
    {
        const auto *table = state->evidence;
        if(base_only || !table || !table->visits || !table->nonzero_seen || !state->options.enabled()
            || state->options.application == amaf_application::selection || axis == 0)
            return {state->base[axis], allowed};
        if(state->version[axis] != table->visits)
        {
            search_trace_phase phase("amaf_ordering");
            auto &ordered = state->learned[axis];
            auto &adjusted = state->adjusted[axis];
            ordered = state->base[axis];
            const double mean = table->sum_reward / static_cast<double>(table->visits);
            for(index_t i : ordered)
            {
                double bonus = 0;
                const auto found = table->entries.find({axis, i});
                if(found != table->entries.end() && found->second.visits)
                {
                    const auto &shared = found->second;
                    bonus = state->options.weight(shared.visits)
                        * (shared.sum_reward / static_cast<double>(shared.visits) - mean);
                    if(state->black) bonus = -bonus;
                }
                adjusted[i] = state->scores[axis][i] + bonus;
            }
            std::sort(ordered.begin(), ordered.end(), [&](index_t a, index_t b) {
                return adjusted[a] != adjusted[b] ? adjusted[a] > adjusted[b]
                    : state->base_rank[axis][a] < state->base_rank[axis][b];
            });
            state->version[axis] = table->visits;
            if(state->diagnostics)
            {
                ++state->diagnostics->ordering_rebuilds;
                state->diagnostics->ordering_changed_axes += ordered != state->base[axis];
            }
        }
        return {state->learned[axis], allowed};
    }

    amaf_HC_ordering for_search() const
    {
        auto result = *this;
        // Existing children were obtained from completed legal actions.
        // In children mode, learned marginal credit never steers discovery
        // into new combinations whose companion moves may be incompatible.
        result.base_only = state->options.application == amaf_application::children;
        return result;
    }

    void observe_child_choice(index_t axis, const integer_set &allowed, index_t chosen) const
    {
        if(base_only || !state->diagnostics || allowed.size() < 2) return;
        ++state->diagnostics->child_choices;
        for(index_t original : detail::ordered_coordinate_range{state->base[axis], allowed})
        {
            state->diagnostics->changed_child_choices += original != chosen;
            break;
        }
    }
};
static_assert(HCOrdering<amaf_HC_ordering>);

struct amaf_node_data
{
    // Only nodal action roots own tables. A temporary node caches its outgoing
    // context's table after the first completed legal action through that node.
    std::unique_ptr<amaf_detail::table> amaf_table;
    amaf_detail::table *amaf_context = nullptr;
    // A ceiling's submitted state is immutable, including after ignition.
    // Roots are rebuilt for every search, so this never crosses searches.
    std::optional<reward_t<rollout_details>> cached_evaluation;
    bool shared_evidence_recorded = false;
    // Evidence through this incoming edge, using the same samples and values
    // as the pooled table even when ordinary UCT backs up different rewards.
    double local_evidence_sum = 0.0;
    std::size_t local_evidence_visits = 0;
};

class amaf_child_selection
{
    amaf_options options;
    std::shared_ptr<amaf_diagnostics> diagnostics;
    bool weighted_exploration;

public:
    explicit amaf_child_selection(amaf_options options_ = {},
        std::shared_ptr<amaf_diagnostics> diagnostics_ = nullptr,
        bool weighted_exploration_ = false)
        : options(options_), diagnostics(std::move(diagnostics_)),
          weighted_exploration(weighted_exploration_)
    {
        options.validate();
    }

    double value_adjustment(double local_sum, std::size_t visits,
        const amaf_detail::statistic &shared, const amaf_node_data &data,
        const amaf_detail::table &table) const
    {
        if(!visits) return 0.0;
        const bool outside = options.credit == amaf_credit::outside;
        const auto local_visits = outside ? data.local_evidence_visits : visits;
        if(shared.visits <= local_visits) return 0.0;
        const double local_mean = local_sum / static_cast<double>(visits);
        double adjustment;
        if(outside)
        {
            const double m = static_cast<double>(shared.visits - local_visits);
            const double outside_mean = (shared.sum_reward - data.local_evidence_sum) / m;
            adjustment = m / (m + 8.0) * (outside_mean - local_mean);
        }
        else
        {
            const double shared_mean = shared.sum_reward / static_cast<double>(shared.visits);
            adjustment = options.credit == amaf_credit::centered
                ? shared_mean - table.sum_reward / static_cast<double>(table.visits)
                : shared_mean - local_mean;
        }
        if(diagnostics)
        {
            ++diagnostics->shared_uses;
            diagnostics->changed_values += adjustment != 0.0;
        }
        return options.weight(visits) * adjustment;
    }

    template<class Node>
    Node *operator()(Node *node) const
    {
        const auto &data = node->get_info().tree_policy_data;
        const auto *table = node->is_nodal() ? data.amaf_table.get() : data.amaf_context;
        const auto adjustment = [&](const Node *child) {
            // Axis zero has only one prefix in each action.
            if(!table || child->get_n() == 0 || !options.enabled()
                || options.application == amaf_application::admission
                || options.application == amaf_application::children)
                return 0.0;
            if(diagnostics) ++diagnostics->lookups;
            const auto found = table->entries.find({child->get_n(), child->get_i()});
            if(found == table->entries.end()) return 0.0;
            const auto &info = child->get_info();
            return value_adjustment(info.sum_reward, info.visits, found->second,
                info.tree_policy_data, *table);
        };
        if(weighted_exploration) return solid_child_selection{}(node, adjustment);
        return uct_detail::best_uct_child(node, [](const Node *) { return 1.0f; }, adjustment);
    }
};

class amaf_backpropagation
{
    amaf_options options;
    std::shared_ptr<amaf_diagnostics> diagnostics;

public:
    explicit amaf_backpropagation(amaf_options options_ = {},
        std::shared_ptr<amaf_diagnostics> diagnostics_ = nullptr)
        : options(options_), diagnostics(std::move(diagnostics_)) {}

    template<class Node, class Result, class Observer>
    void backpropagate(Node *node, const Result &reward, Observer &observer)
    {
        if(options.neural_prior_only)
        {
            auto direct = reward;
            if constexpr(requires { direct.data.end; })
                if(direct.data.end == rollout_details::termination::ACTION_LIMIT)
                    direct.score = 0.0f;
            sum_backpropagation{}.backpropagate(node, direct, observer);
        }
        else sum_backpropagation{}.backpropagate(node, reward, observer);
        if(!options.enabled()) return;
        if(options.unique_leaf_evidence)
        {
            auto &leaf = node->get_info().tree_policy_data;
            if(leaf.shared_evidence_recorded)
            {
                if(diagnostics) ++diagnostics->repeated_evidence_skips;
                return;
            }
            leaf.shared_evidence_recorded = true;
            if(diagnostics) ++diagnostics->unique_evidence_updates;
        }

        // Each segment is one submitted action. Walk from the completed
        // ceiling, including completion edges which PW has not registered.
        // The incoming edge of an ignited ceiling belongs to its PARENT's
        // action context, not to the ceiling's newly built HC context.
        while(node && node->get_parent())
        {
            auto *owner = node->get_parent();
            while(owner && !owner->is_nodal()) owner = owner->get_parent();
            if(!owner) throw std::logic_error("AMAF path has no action root");
            auto &data = owner->get_info().tree_policy_data;
            if(node->get_n() != 0)
            {
                if(!data.amaf_table)
                {
                    data.amaf_table = std::make_unique<amaf_detail::table>();
                    if(diagnostics) ++diagnostics->tables;
                }
                auto *table = data.amaf_table.get();
                table->sum_reward += reward.score;
                table->nonzero_seen = table->nonzero_seen || reward.score != 0.0f;
                ++table->visits;
                if(diagnostics) ++diagnostics->action_updates;
                for(auto *edge = node; edge != owner; edge = edge->get_parent())
                {
                    if(!edge->is_nodal())
                        edge->get_info().tree_policy_data.amaf_context = table;
                    if(edge->get_n() == 0) continue;
                    auto &shared = table->entries[{edge->get_n(), edge->get_i()}];
                    shared.sum_reward += reward.score;
                    ++shared.visits;
                    if(options.credit == amaf_credit::outside)
                    {
                        auto &edge_data = edge->get_info().tree_policy_data;
                        edge_data.local_evidence_sum += reward.score;
                        ++edge_data.local_evidence_visits;
                    }
                    if(diagnostics)
                    {
                        ++diagnostics->coordinate_updates;
                        diagnostics->nonzero_coordinate_updates += reward.score != 0.0f;
                    }
                }
                if(data.ordering)
                    data.ordering->bind(table, options, owner->get_player(), diagnostics);
            }
            node = owner;
        }
    }
};

struct amaf_observer : mcts_observer
{
    std::shared_ptr<amaf_diagnostics> diagnostics;
    explicit amaf_observer(std::shared_ptr<amaf_diagnostics> diagnostics_ = nullptr)
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
            out << "\namaf_stats lookups=" << diagnostics->lookups
                << " shared_uses=" << diagnostics->shared_uses
                << " changed_values=" << diagnostics->changed_values
                << " coordinate_updates=" << diagnostics->coordinate_updates
                << " action_updates=" << diagnostics->action_updates
                << " nonzero_coordinate_updates=" << diagnostics->nonzero_coordinate_updates
                << " tables=" << diagnostics->tables
                << " ordering_rebuilds=" << diagnostics->ordering_rebuilds
                << " ordering_changed_axes=" << diagnostics->ordering_changed_axes
                << " evaluation_cache_hits=" << diagnostics->cache_hits
                << " evaluation_cache_misses=" << diagnostics->cache_misses
                << " child_choices=" << diagnostics->child_choices
                << " changed_child_choices=" << diagnostics->changed_child_choices
                << " unique_evidence_updates=" << diagnostics->unique_evidence_updates
                << " repeated_evidence_skips=" << diagnostics->repeated_evidence_skips;
        return out.str();
    }
};

class amaf_leaf_policy
{
    std::shared_ptr<const neural_model> model;
    float value_scale;
    bool cache_evaluations;
    std::shared_ptr<amaf_diagnostics> diagnostics;

public:
    using result_type = reward_t<rollout_details>;
    explicit amaf_leaf_policy(std::shared_ptr<const neural_model> model_ = nullptr,
        float value_scale_ = 1.0f, bool cache_evaluations_ = false,
        std::shared_ptr<amaf_diagnostics> diagnostics_ = nullptr)
        : model(std::move(model_)), value_scale(value_scale_),
          cache_evaluations(cache_evaluations_), diagnostics(std::move(diagnostics_))
    {
        if(!(value_scale >= 0.0f && value_scale <= 1.0f) || !std::isfinite(value_scale))
            throw std::invalid_argument("neural value scale must be in [0, 1]");
    }
    template<class Observer>
    std::optional<result_type> evaluate(state position, std::stop_token stop, Observer &)
    {
        return neural_detail::evaluate_position(model.get(), value_scale,
            std::move(position), stop);
    }

    template<class Node, class Observer>
    std::optional<result_type> evaluate_ceiling(Node &node, std::stop_token stop, Observer &)
    {
        return neural_detail::evaluate_ceiling(node, model.get(), value_scale,
            cache_evaluations, stop, cache_evaluations ? diagnostics.get() : nullptr);
    }
};

using amaf_tree_policy = progressive_widening_tree_policy<
    amaf_HC_ordering, amaf_child_selection, amaf_node_data>;
using amaf_mcts_engine = basic_mcts_engine<
    amaf_tree_policy, amaf_leaf_policy, amaf_backpropagation,
    most_visited_selection, amaf_observer>;

class amaf_pw_engine final : public amaf_mcts_engine
{
    amaf_pw_engine(std::unique_ptr<io_handler> io,
        std::optional<std::uint32_t> seed, double constant, double alpha,
        amaf_options options, std::shared_ptr<const neural_model> model,
        float value_scale, bool cache_evaluations, HC_build_options build_options,
        std::size_t prefix_budget, bool weighted_exploration,
        std::shared_ptr<amaf_diagnostics> diagnostics)
        : amaf_mcts_engine(amaf_leaf_policy{std::move(model), value_scale, cache_evaluations, diagnostics},
            amaf_tree_policy{seed, constant, alpha,
                solid_scores, amaf_child_selection{options, diagnostics, weighted_exploration},
                build_options, prefix_budget},
            amaf_backpropagation{options, diagnostics}, {},
            amaf_observer{diagnostics}, std::move(io)) {}

public:
    amaf_pw_engine(std::unique_ptr<io_handler> io,
        std::optional<std::uint32_t> seed, double constant, double alpha,
        amaf_options options,
        std::shared_ptr<const neural_model> model = nullptr,
        float value_scale = 1.0f, bool cache_evaluations = false,
        HC_build_options build_options = {},
        std::size_t prefix_budget = default_fine_prefix_budget, bool weighted_exploration = false)
        : amaf_pw_engine(std::move(io), seed, constant, alpha, options,
            std::move(model), value_scale, cache_evaluations, build_options, prefix_budget,
            weighted_exploration, std::make_shared<amaf_diagnostics>()) {}
};

static_assert(DefaultPolicy<amaf_leaf_policy, amaf_observer>);
static_assert(TreePolicy<amaf_tree_policy, amaf_observer>);

#endif /* AMAF_H */

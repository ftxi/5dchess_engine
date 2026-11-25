#include "neural.h"
#include "hash.h"
#include "move_evaluation.h"

#include <deque>

namespace
{
constexpr unsigned max_successors = 16;
constexpr unsigned max_queries = 64;
constexpr unsigned max_work = 2048;
constexpr unsigned request_work = 32;
constexpr unsigned width = 4;
using result_type = reward_t<rollout_details>;

struct tactic
{
    index_t axis, coordinate;
    float priority;
    bool recapture;
};

// A query with no fixed coordinate can find evasions and checks that only
// appear after several semimoves have been played together.
struct candidates
{
    HC_info info;
    HC_info::completion_space space;
    std::optional<scored_HC_ordering> ordering;
    std::deque<std::optional<tactic>> requests;

    candidates(std::pair<HC_info, search_space> built, bool checked,
        const std::vector<vec4> &targets, std::stop_token stop)
        : info(std::move(built.first)), space(std::move(built.second))
    {
        const auto &universe = info.get_universe();
        auto weights = capture_check_weights;
        // Outside check, classification below also supplies the check bonus.
        // Avoid constructing and scanning a second check-position view.
        if(!checked) weights.values[move_evaluation_weights::CHECK] = 0.0f;
        move_evaluator evaluator(info, weights, capture_ordering::mvvlva);
        auto scores = evaluator.build_score_table(stop);
        if(!scores) return;
        std::mt19937 rng(0);
        // Keep an unrestricted query even outside check: several quiet
        // semimoves can give check only after their boards advance together.
        if(checked || info.dimension() > 1) requests.push_back(std::nullopt);
        if(checked)
        {
            ordering.emplace(universe, *scores, rng);
            return;
        }

        std::vector<tactic> captures, checks;
        semimove_feature features(info);
        for(index_t axis = 0; axis < info.dimension(); ++axis)
        {
            for(index_t i : universe[axis])
            {
                if(stop.stop_requested()) return;
                const auto boards = info.get_move_boards(axis, i);
                if(!boards) continue;
                const auto &move = boards->move;
                const bool capture = move.captured_piece(info.get_state()) != NO_PIECE;
                const bool check = features.is_check(axis, i);
                if(check) (*scores)[axis][i] += capture_check_weights.values[move_evaluation_weights::CHECK];
                const float priority = (*scores)[axis][i];
                if(capture)
                {
                    const bool recapture = std::ranges::find(targets, move.to) != targets.end();
                    captures.push_back({axis, i, priority - (check ? capture_check_weights.values[
                        move_evaluation_weights::CHECK] : 0.0f), recapture});
                }
                if(check) checks.push_back({axis, i, priority, false});
            }
        }
        ordering.emplace(universe, *scores, rng);
        const auto better = [](const tactic &a, const tactic &b) {
            return std::tie(a.recapture, a.priority) > std::tie(b.recapture, b.priority);
        };
        std::stable_sort(captures.begin(), captures.end(), better);
        std::stable_sort(checks.begin(), checks.end(), better);
        for(std::size_t i = 0; i < std::max(captures.size(), checks.size()); ++i)
        {
            if(i < captures.size()) requests.push_back(captures[i]);
            if(i < checks.size()) requests.push_back(checks[i]);
        }
    }

    std::optional<moveseq> next(quiescence_stats &stats)
    {
        if(requests.empty()) return std::nullopt;
        const auto request = requests.front();
        requests.pop_front();
        HC domain = info.get_universe();
        if(request) domain[request->axis] = integer_set{request->coordinate};
        ++stats.queries;
        auto found = info.find_witness_with_budget(space, domain, *ordering, nullptr,
            std::min(request_work, max_work - stats.work));
        stats.work += static_cast<unsigned>(found.work);
        if((found.budget_exhausted && !found.witness) || (!request && found.witness))
            requests.push_back(request);
        if(!found.witness) return std::nullopt;

        // This forest is private to one position. Exclude precisely the used
        // point from every request, including different anchors in the action.
        slice used;
        for(index_t n = 0; n < info.dimension(); ++n)
            used.fix_axis(n, integer_set{(*found.witness)[n]});
        space.cells.remove_slice_globally(used);
        return info.to_action(*found.witness);
    }
};

struct tactical_node
{
    state position;
    result_type evaluation;
    unsigned depth;
    bool checked;
    std::uint64_t hash;
    std::vector<vec4> recapture_targets;
    std::unique_ptr<candidates> generator;
    std::vector<std::unique_ptr<tactical_node>> children;

    float value() const
    {
        if(children.empty())
        {
            // Static evaluation already checked terminality. A bounded query
            // that has not produced an evasion cannot establish checkmate.
            return evaluation.score;
        }
        float best = checked ? (position.get_present().c() ? WINNING_SCORE : -WINNING_SCORE)
                             : evaluation.score;
        for(const auto &child : children)
            best = position.get_present().c() ? std::min(best, child->value())
                                              : std::max(best, child->value());
        return best;
    }

    std::vector<tactical_node *> ranked() const
    {
        std::vector<tactical_node *> result;
        for(const auto &child : children) result.push_back(child.get());
        std::stable_sort(result.begin(), result.end(), [&](auto *a, auto *b) {
            return position.get_present().c() ? a->value() < b->value() : a->value() > b->value();
        });
        return result;
    }
};

struct tournament
{
    const neural_model &model;
    unsigned limit;
    std::stop_token stop;
    quiescence_stats &stats;

    bool available() const
    {
        return !stop.stop_requested() && stats.successors < max_successors
            && stats.queries < max_queries && stats.work < max_work;
    }

    tactical_node *expand(tactical_node &node)
    {
        if(!available() || node.depth >= limit
            || node.evaluation.data.end != rollout_details::termination::ACTION_LIMIT) return nullptr;
        if(!node.generator)
            node.generator = std::make_unique<candidates>(
                HC_info::build_HC(node.position, {.remove_check = true}),
                node.checked, node.recapture_targets, stop);
        if(!available() || node.generator->requests.empty()) return nullptr;
        auto action = node.generator->next(stats);
        if(!action) return nullptr;
        state successor = node.position;
        std::vector<vec4> targets;
        for(const auto &move : *action)
        {
            if(move.captured_piece(successor) != NO_PIECE)
            {
                const auto at = turn{move.to.t(), successor.get_present().c()}.next_turn();
                targets.emplace_back(move.to.x(), move.to.y(), at.t(), move.new_position(successor).l());
            }
            successor.apply_move<true>(move);
        }
        successor.submit<true>();
        const bool checked = successor.has_phantom_check();
        if(!node.checked && targets.empty() && !checked) return nullptr;
        const auto hash = hash_state(successor);
        for(const auto &other : node.children)
            if(other->hash == hash && other->position.get_present() == successor.get_present()
                && other->position.show_fen() == successor.show_fen()) return nullptr;
        const auto evaluation = model.evaluate_position(successor, stop);
        if(!evaluation) return nullptr;
        ++stats.successors;
        stats.deepest = std::max(stats.deepest, node.depth + 1);
        auto &child = node.children.emplace_back(std::make_unique<tactical_node>(
            std::move(successor), *evaluation, node.depth + 1, checked, hash, std::move(targets)));
        return child.get();
    }

    void run(tactical_node &root)
    {
        std::vector<tactical_node *> survivors;
        unsigned round = 0;
        while(available())
        {
            const unsigned before = stats.queries;
            for(auto *child : survivors)
            {
                if(child->children.empty() || round % 2 == 0) expand(*child);
                else
                {
                    // Follow the strongest examined reply for one more ply.
                    auto *line = child->ranked().front();
                    while(!line->children.empty() && line->depth + 1 < limit)
                        line = line->ranked().front();
                    if(!expand(*line)) expand(*child);
                }
            }
            // Half the places go to fresh alternatives after the opening round.
            std::vector<tactical_node *> admitted;
            for(unsigned i = survivors.size(); i < width; ++i)
                if(auto *child = expand(root)) admitted.push_back(child);
            for(auto *child : admitted) expand(*child);
            if(stats.queries == before) break;
            survivors = root.ranked();
            if(survivors.size() > width / 2) survivors.resize(width / 2);
            ++round;
        }
    }
};
} /* anonymous namespace */

std::optional<reward_t<rollout_details>> evaluate_quiescence(
    const neural_model &model, state position, unsigned depth, std::stop_token stop,
    quiescence_stats &stats)
{
    stats = {};
    auto evaluation = model.evaluate_position(position, stop);
    if(!evaluation || evaluation->data.end != rollout_details::termination::ACTION_LIMIT || !depth)
        return evaluation;
    const bool checked = position.has_phantom_check();
    tactical_node root{std::move(position), *evaluation, 0, checked, 0, {}, {}, {}};
    tournament{model, depth, stop, stats}.run(root);
    // The completed static result can still finish the current MCTS iteration
    // when the move deadline interrupts tactical refinement.
    if(stop.stop_requested()) return evaluation;
    evaluation->score = root.value();
    return evaluation;
}

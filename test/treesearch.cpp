#include "state.h"
#include "pgnparser.h"
#include "game.h"
#include <algorithm>
#include <cassert>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>

std::string str = R"(
[Board "Very Small - Open"]
1. Rxb4 / Rxb4 
)";

using score_t = double;

score_t score_from_status(match_status_t status, bool root_is_black)
{
    switch(status)
    {
        case match_status_t::WHITE_WINS:
            return root_is_black ? -1.0 : 1.0;
        case match_status_t::BLACK_WINS:
            return root_is_black ? 1.0 : -1.0;
        case match_status_t::STALEMATE:
            return 0.0;
        case match_status_t::PLAYING:
            return 0.0;
    }
    return 0.0;
}

score_t alpha_beta_score(gnode<score_t>* node, unsigned int depth, score_t alpha, score_t beta, unsigned int ply, bool root_is_black)
{
    match_status_t status = node->get_match_status();
    if(depth == 0 || status != match_status_t::PLAYING)
    {
        const score_t score = score_from_status(status, root_is_black);
        node->set_info(score);
        return score;
    }

    while(node->new_child().has_value())
    {
    }

    const auto children = node->get_children();
    if(children.empty())
    {
        const score_t score = score_from_status(node->get_match_status(), root_is_black);
        node->set_info(score);
        return score;
    }

    const bool maximizing = (ply % 2 == 0);
    if(maximizing)
    {
        score_t best = std::numeric_limits<score_t>::lowest();
        for(auto* child : children)
        {
            const score_t value = alpha_beta_score(child, depth - 1, alpha, beta, ply + 1, root_is_black);
            best = std::max(best, value);
            alpha = std::max(alpha, best);
            if(alpha >= beta)
            {
                break;
            }
        }
        node->set_info(best);
        return best;
    }

    score_t best = std::numeric_limits<score_t>::max();
    for(auto* child : children)
    {
        const score_t value = alpha_beta_score(child, depth - 1, alpha, beta, ply + 1, root_is_black);
        best = std::min(best, value);
        beta = std::min(beta, best);
        if(alpha >= beta)
        {
            break;
        }
    }
    node->set_info(best);
    return best;
}

std::unique_ptr<gnode<score_t>> build_strategy_tree(
    const gnode<score_t>* source,
    gnode<score_t>* parent,
    unsigned int depth,
    unsigned int ply)
{
    std::unique_ptr<gnode<score_t>> out = parent
        ? gnode<score_t>::create_child(parent, source->get_state(), source->get_action(), source->get_info())
        : gnode<score_t>::create_root(source->get_state(), source->get_info());

    if(depth == 0 || source->get_match_status() != match_status_t::PLAYING)
    {
        return out;
    }

    const auto children = source->get_children();
    if(children.empty())
    {
        return out;
    }

    const bool maximizing = (ply % 2 == 0);
    if(maximizing)
    {
        auto it = std::max_element(children.begin(), children.end(), [](const gnode<score_t>* lhs, const gnode<score_t>* rhs)
        {
            return lhs->get_info() < rhs->get_info();
        });
        if(it != children.end())
        {
            out->add_child(build_strategy_tree(*it, out.get(), depth - 1, ply + 1));
        }
        return out;
    }

    for(const auto* child : children)
    {
        out->add_child(build_strategy_tree(child, out.get(), depth - 1, ply + 1));
    }
    return out;
}

std::unique_ptr<gnode<score_t>> tree_search(gnode<score_t>* node, unsigned int depth)
{
    if(node == nullptr)
    {
        return nullptr;
    }

    const bool root_is_black = std::get<1>(node->get_state().get_present());
    alpha_beta_score(node,
        depth,
        std::numeric_limits<score_t>::lowest(),
        std::numeric_limits<score_t>::max(),
        0,
        root_is_black);

    return build_strategy_tree(node, nullptr, depth, 0);
}

int main()
{
    state s(*pgnparser(str).parse_game());
    auto root = gnode<score_t>::create_root(s, 0.0);

    constexpr unsigned int depth = 4;
    auto strategy = tree_search(root.get(), depth);

    assert(strategy != nullptr);
    const auto root_children = strategy->get_children();
    assert(!root_children.empty());

    const auto opponent_layer = root_children.front()->get_children();
    assert(!opponent_layer.empty());

    std::cout << "Root score: " << strategy->get_info() << "\n";
    std::cout << "Best root actions kept: " << root_children.size() << "\n";
    std::cout << "Opponent responses for first line: " << opponent_layer.size() << "\n";
    std::cout << strategy->to_string([](score_t s) {
        std::ostringstream oss;
        oss << "{score:" << std::fixed << std::setprecision(2) << s << "}";
        return oss.str();
    }) << "\n";
    return 0;
}
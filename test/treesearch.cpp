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


double score_from_status(match_status_t status)
{
    switch(status)
    {
        case match_status_t::WHITE_WINS:
            return 1.0;
        case match_status_t::BLACK_WINS:
            return -1.0;
        case match_status_t::STALEMATE:
            return 0.0;
        case match_status_t::PLAYING:
            return 0.0;
    }
    return 0.0;
}

struct search_info {
    double score;
    double alpha;
    double beta;
    int depth;
    std::optional<action> best_action;
};

// std::unique_ptr<gnode<search_info>> tree_search(
//     const state &s,
//     int depth,
//     double alpha = -std::numeric_limits<double>::infinity(),
//     double beta = std::numeric_limits<double>::infinity()
// )
// {
//     auto node = gnode<search_info>::create_root(s, search_info{
//         score_from_status(s.get_ma()),
//         alpha,
//         beta,
//         depth,
//         std::nullopt
//     });
//     if(depth == 0)
//         return node;
    
//     const auto actions = s.can_submit();
//     if(!actions.has_value())
//         return node;

//     for(const auto &act : actions.value().get_moves())
//     {
//         const auto new_state_opt = s.can_apply(act);
//         if(!new_state_opt.has_value())
//             continue;
//         const auto new_state = std::move(new_state_opt.value());
//         const auto child_node = tree_search(new_state, depth - 1, -beta, -alpha);
//         const auto child_score = -child_node->get_info().score;
//         if(child_score > node->get_info().score)
//         {
//             node->get_info().score = child_score;
//             node->get_info().best_action = act;
//         }
//         node->get_info().alpha = std::max(node->get_info().alpha, child_score);
//         if(node->get_info().alpha >= node->get_info().beta)
//             break; // beta cut-off
//     }
//     return node;
// }

int main()
{
    state s(*pgnparser(str).parse_game());
    // auto root = gnode<score_t>::create_root(s, 0.0);

    // constexpr unsigned int depth = 3;
    // auto strategy = tree_search(root.get(), depth);

    // assert(strategy != nullptr);
    // const auto root_children = strategy->get_children();
    // assert(!root_children.empty());

    // const auto opponent_layer = root_children.front()->get_children();
    // assert(!opponent_layer.empty());

    // std::cout << "Root score: " << strategy->get_info() << "\n";
    // std::cout << "Best root actions kept: " << root_children.size() << "\n";
    // std::cout << "Opponent responses for first line: " << opponent_layer.size() << "\n";
    // std::cout << strategy->to_string([](score_t s) {
    //     std::ostringstream oss;
    //     oss << "{score:" << std::fixed << std::setprecision(2) << s << "}";
    //     return oss.str();
    // }) << "\n";
    return 0;
}
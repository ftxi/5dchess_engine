#include <iostream>
#include <cassert>
#include <limits>
#include <cmath>
#include <stop_token>
#include "state.h"
#include "finetree.h"
#include "hypercuboid.h"
#include "mcts.h"
#include "variants.h"
#include "utils.h"

// =============================================================
//  Stub io_handler — lets us construct mcts_engine directly
// =============================================================
static bool stub_is_open_called = false;

class stub_io_handler : public io_handler
{
public:
    std::string read_line() override
    {
        return "";
    }
    void write_line(const std::string &line) override
    {
        std::cerr << "[stub_io] " << line << "\n";
    }
    bool is_open() override
    {
        stub_is_open_called = true;
        return true;
    }
};

// =============================================================
//  Inline helpers
// =============================================================

using node_t = fine_node<mcts_node_info>;

constexpr float WINNING_SCORE = 100000.0f;
constexpr int ROLLOUT_MAX_ACTIONS = 200;
constexpr float EXPLORATION_CONSTANT = 1.4142135623730951f;

static float test_uct(float sum_reward, std::size_t visits, std::size_t parent_visits)
{
    if(visits == 0) return std::numeric_limits<float>::infinity();
    const float average_reward = sum_reward / static_cast<float>(visits);
    const float logParent = std::logf(static_cast<float>(parent_visits) + 1.0f);
    const float exploration = EXPLORATION_CONSTANT * std::sqrtf(logParent / static_cast<float>(visits));
    return average_reward + exploration;
}

// =============================================================
//  Duplicated MCTS functions with verbose output
// =============================================================

static node_t *verbose_expand(node_t *node, std::stop_token stop_token)
{
    std::cerr << "  [verbose_expand] node=" << node->print_semimove()
              << " fully_expanded=" << node->get_info().fully_expanded
              << " all_children_included=" << node->get_info().all_children_included
              << " num_children=" << node->get_children().size()
              << "\n";
    if(node->get_info().fully_expanded)
    {
        std::cerr << "  [verbose_expand] fully_expanded -> nullptr\n";
        return nullptr;
    }
    if(!node->get_info().all_children_included)
    {
        for(node_t *child : node->get_children())
        {
            std::cerr << "  [verbose_expand] checking child "
                      << " included=" << child->get_info().is_included
                      << " visits=" << child->get_info().visits
                      << "\n";
            if(!child->get_info().is_included)
            {
                child->get_info().is_included = true;
                std::cerr << "  [verbose_expand] returning unincluded child\n";
                return child;
            }
        }
        node->get_info().all_children_included = true;
        std::cerr << "  [verbose_expand] all children included, falling through\n";
    }
    if(stop_token.stop_requested())
    {
        std::cerr << "  [verbose_expand] stop requested -> nullptr\n";
        return nullptr;
    }
    if(node->is_ceiling() && !node->is_nodal())
    {
        std::cerr << "  [verbose_expand] igniting ceiling node\n";
        node->ignite();
        node->get_info().player = node->get_context()->hc_info.s.get_present().second;
        std::cerr << "  [verbose_expand] after ignite, player=" << node->get_info().player << "\n";
    }
    std::cerr << "  [verbose_expand] calling node->search().first()...\n";
    if(auto i_opt = node->search().first())
    {
        node_t *child = node->get_child(*i_opt);
        std::cerr << "  [verbose_expand] search() found child i=" << *i_opt
                  << " child=" << (child ? "non-null" : "NULL") << "\n";
        assert(child != nullptr);
        return child;
    }
    std::cerr << "  [verbose_expand] search() found nothing, marking fully_expanded\n";
    node->get_info().fully_expanded = true;
    return nullptr;
}

static node_t *verbose_best_child(node_t *node)
{
    std::cerr << "  [verbose_best_child] node visits=" << node->get_info().visits
              << " player=" << node->get_info().player
              << "\n";
    node_t *best = nullptr;
    bool max_player = node->get_info().player;
    float best_val = max_player ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity();
    for(node_t *child : node->get_children())
    {
        const auto &info = child->get_info();
        if(info.visits == 0)
        {
            std::cerr << "  [verbose_best_child] skip child (unvisited)\n";
            continue;
        }
        float score = test_uct(info.sum_reward, info.visits, node->get_info().visits);
        bool better = max_player ? (score > best_val) : (score < best_val);
        std::cerr << "  [verbose_best_child] child reward=" << info.sum_reward
                  << " visits=" << info.visits
                  << " uct=" << score << " better=" << better << "\n";
        if(better)
        {
            best_val = score;
            best = child;
        }
    }
    if(best) std::cerr << "  [verbose_best_child] result: (found)\n";
    else     std::cerr << "  [verbose_best_child] result: nullptr\n";
    return best;
}

static node_t *verbose_tree_policy(node_t *node, std::stop_token stop_token)
{
    std::cerr << "[verbose_tree_policy] start\n";
    int depth = 0;
    while(!node->is_terminal() && !stop_token.stop_requested())
    {
        std::cerr << "[verbose_tree_policy] depth=" << depth
                  << " terminal=" << node->is_terminal()
                  << " nodal=" << node->is_nodal()
                  << " ceiling=" << node->is_ceiling()
                  << "\n";
        node_t *next_node = verbose_expand(node, stop_token);
        if(next_node)
        {
            std::cerr << "[verbose_tree_policy] expand returned leaf\n";
            return next_node;
        }
        node_t *bc = verbose_best_child(node);
        if(bc == nullptr)
        {
            std::cerr << "[verbose_tree_policy] best_child returned nullptr at depth " << depth << "\n";
            return node;
        }
        node = bc;
        depth++;
    }
    std::cerr << "[verbose_tree_policy] reached terminal or stop-requested\n";
    return node;
}

struct verbose_sim_result
{
    float outcome;
    int actions;
    bool limit_reached;
    bool aborted;
};

static verbose_sim_result verbose_default_policy(node_t *node, int max_actions, std::stop_token stop_token)
{
    std::cerr << "[verbose_default_policy] start max_actions=" << max_actions << "\n";
    node_t *ceiling_node = node->get_nearby_ceiling();
    if(!ceiling_node)
    {
        std::cerr << "[verbose_default_policy] get_nearby_ceiling returned nullptr! ABORT\n";
        return {0.0f, 0, true, true};
    }
    std::cerr << "[verbose_default_policy] ceiling_node is_nodal=" << ceiling_node->is_nodal() << "\n";
    if(!ceiling_node->is_nodal())
    {
        std::cerr << "[verbose_default_policy] igniting ceiling node\n";
        ceiling_node->ignite();
        std::cerr << "[verbose_default_policy] after ignite, nodal=" << ceiling_node->is_nodal() << "\n";
    }
    state s = ceiling_node->get_context()->hc_info.s;
    std::cerr << "[verbose_default_policy] state present=" << s.get_present() << "\n";

    int num_actions;
    for(num_actions = 0; num_actions < max_actions; num_actions++)
    {
        if(stop_token.stop_requested())
        {
            std::cerr << "[verbose_default_policy] stop requested at action " << num_actions << "\n";
            return {0.0f, num_actions, true, true};
        }
        [[maybe_unused]] auto [present, player] = s.get_present();
        auto [w, ss] = HC_info::build_HC(s);
        w.shuffle(ss);
        if(auto mvs = w.iterative_search(ss).first())
        {
            for(full_move fm : *mvs)
            {
                s.apply_move(fm);
            }
            s.submit();
        }
        else
        {
            float outcome = player ? -WINNING_SCORE : WINNING_SCORE;
            std::cerr << "[verbose_default_policy] no legal moves, outcome=" << outcome
                      << " at depth " << num_actions << "\n";
            return {outcome, num_actions, false, false};
        }
    }
    std::cerr << "[verbose_default_policy] max_actions reached, returning 0\n";
    return {0.0f, num_actions, true, false};
}

static void verbose_backpropagate(node_t *node, float outcome)
{
    std::cerr << "[verbose_backpropagate] outcome=" << outcome << "\n";
    while(node != nullptr)
    {
        auto &info = node->get_info();
        info.visits++;
        info.sum_reward += outcome;
        std::cerr << "[verbose_backpropagate] visits now=" << info.visits
                  << " reward now=" << info.sum_reward << "\n";
        node = node->get_parent();
    }
}

// =============================================================
//  Test 1:  Create startpos state
// =============================================================
static bool test_startpos_state()
{
    std::cerr << "\n==== Test 1: Create startpos state ====\n";
    state s(*create_multiverse_from_variant_setup(default_variants.at("Standard - Turn Zero")));
    auto [present_t, present_c] = s.get_present();
    std::cerr << "State created. Present t=" << present_t << " player=" << present_c << "\n";

    auto [w, ss] = HC_info::build_HC(s);
    w.shuffle(ss);
    std::cerr << "HC built. Searching for one action...\n";
    if(auto mvs = w.iterative_search(ss).first())
    {
        std::cerr << "Found action with " << mvs->size() << " moves\n";
    }
    else
    {
        std::cerr << "ERROR: No legal action from startpos!\n";
        return false;
    }
    std::cerr << "Test 1 PASSED\n";
    return true;
}

// =============================================================
//  Test 2:  fine_node construction and basic navigation
// =============================================================
static bool test_fine_node_basics()
{
    std::cerr << "\n==== Test 2: fine_node basics ====\n";
    state s(*create_multiverse_from_variant_setup(default_variants.at("Standard - Turn Zero")));

    std::cerr << "Creating root fine_node<mcts_node_info>...\n";
    auto root = fine_node<mcts_node_info>::make_root(s);
    root->get_info().player = s.get_present().second;
    std::cerr << "Root created. is_nodal=" << root->is_nodal()
              << " is_ceiling=" << root->is_ceiling() << "\n";

    std::cerr << "Calling is_terminal()...\n";
    bool terminal = root->is_terminal();
    std::cerr << "is_terminal=" << terminal << "\n";
    if(terminal)
    {
        std::cerr << "ERROR: startpos should not be terminal!\n";
        return false;
    }

    std::cerr << "Root children after is_terminal: " << root->get_children().size() << "\n";
    for(auto *child : root->get_children())
    {
        std::cerr << "  child n=" << child->get_n() << " i=" << child->get_i()
                  << " is_ceiling=" << child->is_ceiling()
                  << " is_nodal=" << child->is_nodal()
                  << " included=" << child->get_info().is_included
                  << "\n";
    }

    if(root->get_children().empty())
    {
        std::cerr << "ERROR: no children after is_terminal!\n";
        return false;
    }

    // navigate down to ceiling
    node_t *current = root->get_children()[0];
    int depth = 0;
    while(current && !current->is_ceiling())
    {
        depth++;
        std::cerr << "  level " << depth << ": n=" << current->get_n()
                  << " children=" << current->get_children().size() << "\n";
        if(current->get_children().empty())
        {
            std::cerr << "  ERROR: chain ended before ceiling at depth " << depth << "\n";
            return false;
        }
        current = current->get_children()[0];
    }
    if(current && current->is_ceiling())
    {
        std::cerr << "Reached ceiling at depth " << depth << "\n";
    }
    std::cerr << "Test 2 PASSED\n";
    return true;
}

// =============================================================
//  Test 3:  Rollout (default_policy) in isolation
// =============================================================
static bool test_rollout_isolation()
{
    std::cerr << "\n==== Test 3: Rollout isolation ====\n";
    state s(*create_multiverse_from_variant_setup(default_variants.at("Standard - Turn Zero")));

    auto root = fine_node<mcts_node_info>::make_root(s);
    root->get_info().player = s.get_present().second;

    std::cerr << "Calling is_terminal() to seed tree...\n";
    root->is_terminal();

    if(root->get_children().empty())
    {
        std::cerr << "ERROR: no children after is_terminal!\n";
        return false;
    }

    node_t *leaf = root->get_children()[0];
    std::cerr << "First leaf obtained\n";

    std::stop_source src;
    auto result = verbose_default_policy(leaf, ROLLOUT_MAX_ACTIONS, src.get_token());
    std::cerr << "Result: outcome=" << result.outcome
              << " actions=" << result.actions
              << " aborted=" << result.aborted << "\n";

    if(result.aborted)
    {
        std::cerr << "ROLLOUT WAS ABORTED!\n";
        return false;
    }
    std::cerr << "Test 3 PASSED\n";
    return true;
}

// =============================================================
//  Test 4:  Full MCTS pipeline (5 iterations)
// =============================================================
static bool test_full_mcts_pipeline()
{
    std::cerr << "\n==== Test 4: Full MCTS pipeline (5 iterations) ====\n";
    state s(*create_multiverse_from_variant_setup(default_variants.at("Standard - Turn Zero")));

    auto root = fine_node<mcts_node_info>::make_root(s);
    root->get_info().player = s.get_present().second;

    std::stop_source src;
    auto token = src.get_token();

    const int iterations = 5;
    for(int it = 0; it < iterations; it++)
    {
        std::cerr << "\n--- Iteration " << it << " ---\n";
        node_t *leaf = verbose_tree_policy(root.get(), token);
        if(leaf == nullptr)
        {
            std::cerr << "ERROR: tree_policy returned nullptr\n";
            return false;
        }

        auto sim = verbose_default_policy(leaf, ROLLOUT_MAX_ACTIONS, token);
        if(sim.aborted)
        {
            std::cerr << "ERROR: rollout aborted\n";
            return false;
        }
        verbose_backpropagate(leaf, sim.outcome);
    }

    std::cerr << "\n--- After iterations ---\n";
    std::cerr << "Root visits=" << root->get_info().visits
              << " reward=" << root->get_info().sum_reward
              << " children=" << root->get_children().size() << "\n";

    // Find best move
    node_t *current = root.get();
    node_t *prev = nullptr;
    while(current && !current->is_ceiling())
    {
        prev = current;
        current = verbose_best_child(current);
    }
    if(!current && prev)
    {
        std::cerr << "Trying get_nearby_ceiling\n";
        current = prev->get_nearby_ceiling();
    }
    if(current)
    {
        moveseq best_moves = current->to_action();
        std::cerr << "Best moves: " << best_moves.size() << "\n";
        std::cerr << "Test 4 PASSED\n";
        return true;
    }
    else
    {
        std::cerr << "ERROR: could not find a best move!\n";
        return false;
    }
}

// =============================================================
//  Test 5:  Ceiling ignition
// =============================================================
static bool test_ceiling_ignition()
{
    std::cerr << "\n==== Test 5: Ceiling ignition ====\n";
    state s(*create_multiverse_from_variant_setup(default_variants.at("Standard - Turn Zero")));

    auto root = fine_node<mcts_node_info>::make_root(s);
    root->get_info().player = s.get_present().second;

    root->is_terminal();

    node_t *node = root.get();
    while(node && !node->is_ceiling())
    {
        if(node->get_children().empty()) break;
        node = node->get_children()[0];
    }
    if(!node || !node->is_ceiling())
    {
        std::cerr << "ERROR: Could not reach a ceiling node\n";
        return false;
    }
    std::cerr << "Ceiling node reached. Calling ignite()...\n";
    node->ignite();
    std::cerr << "After ignite: is_nodal=" << node->is_nodal() << "\n";

    node->get_info().player = node->get_context()->hc_info.s.get_present().second;
    std::cerr << "Player after ignite: " << (node->get_info().player ? "white" : "black") << "\n";
    std::cerr << "Test 5 PASSED\n";
    return true;
}

// =============================================================
//  Test 6:  Expand after is_terminal
// =============================================================
static bool test_expand_after_is_terminal()
{
    std::cerr << "\n==== Test 6: expand() after is_terminal() ====\n";
    state s(*create_multiverse_from_variant_setup(default_variants.at("Standard - Turn Zero")));

    auto root = fine_node<mcts_node_info>::make_root(s);
    root->get_info().player = s.get_present().second;

    std::cerr << "Step 1: is_terminal() -> "; std::cerr.flush();
    bool term = root->is_terminal();
    std::cerr << (term ? "true" : "false") << "\n";
    std::cerr << "  children=" << root->get_children().size() << "\n";

    std::cerr << "Step 2: MCTS expand\n";
    std::stop_source src;
    node_t *expanded = verbose_expand(root.get(), src.get_token());
    if(!expanded)
    {
        std::cerr << "  expand returned nullptr\n";
        return false;
    }

    std::cerr << "Step 3: Second MCTS expand (should create new branch)\n";
    node_t *expanded2 = verbose_expand(root.get(), src.get_token());
    if(expanded2)
    {
        std::cerr << "  expand returned child\n";
    }
    else
    {
        std::cerr << "  expand returned nullptr (fewer branches than expected)\n";
    }

    std::cerr << "Test 6 PASSED\n";
    return true;
}

// =============================================================
//  Test 7:  Engine-level integration via stub
// =============================================================
static bool test_engine_integration()
{
    std::cerr << "\n==== Test 7: Engine-level integration ====\n";

    stub_is_open_called = false;
    auto io = std::make_unique<stub_io_handler>();
    mcts_engine engine(std::move(io));

    engine.start_new_game();
    engine.set_position("startpos", "");
    std::cerr << "Position set to startpos\n";

    std::stop_source src;
    auto result = engine.find_best_move(std::optional<int>(1), std::nullopt, src.get_token());
    if(result.has_value())
    {
        std::cerr << "Engine found a move!\n";
        for(const ext_move &em : result->get_moves())
            std::cerr << "  " << em.to_string() << "\n";
    }
    else
    {
        std::cerr << "Engine returned no best move (nullopt)\n";
    }

    // Diagnostic: print root children visit counts
    // We need to access the root through the mcts_engine.
    // Since root is a private member, we can't.
    // But we CAN re-verify by running find_best_move again with verbose from our own re-implementation.
    // For now, just pass/fail based on result.
    std::cerr << "Test 7 " << (result.has_value() ? "PASSED" : "FAILED") << "\n";
    return result.has_value();
}


// =============================================================
//  Test 8:  Debug best_child — manually inspect children after MCTS
// =============================================================
static bool test_best_child_debug()
{
    std::cerr << "\n==== Test 8: best_child debug ====\n";
    state s(*create_multiverse_from_variant_setup(default_variants.at("Standard - Turn Zero")));

    auto root = fine_node<mcts_node_info>::make_root(s);
    root->get_info().player = s.get_present().second;

    std::stop_source src;
    auto token = src.get_token();

    const int iterations = 20; // match the full exploration count
    for(int it = 0; it < iterations; it++)
    {
        node_t *leaf = verbose_tree_policy(root.get(), token);
        if(leaf == nullptr)
        {
            std::cerr << "ERROR: tree_policy returned nullptr at iteration " << it << "\n";
            return false;
        }
        auto sim = verbose_default_policy(leaf, ROLLOUT_MAX_ACTIONS, token);
        if(sim.aborted)
        {
            std::cerr << "ERROR: rollout aborted at iteration " << it << "\n";
            return false;
        }
        verbose_backpropagate(leaf, sim.outcome);
    }

    std::cerr << "\n=== After " << iterations << " iterations ===\n";
    std::cerr << "Root visits=" << root->get_info().visits << " children=" << root->get_children().size() << "\n";

    // Now manually test best_child
    std::cerr << "Manually inspecting children:\n";
    bool max_player = root->get_info().player;

    float best_val = max_player ? -std::numeric_limits<float>::infinity() : std::numeric_limits<float>::infinity();
    node_t *bc = nullptr;
    int found_visited = 0;
    for(node_t *child : root->get_children())
    {
        const auto &info = child->get_info();
        std::cerr << "  child visits=" << info.visits
                  << " reward=" << info.sum_reward
                  << " included=" << info.is_included
                  << " n=" << child->get_n()
                  << " i=" << child->get_i()
                  << " is_ceiling=" << child->is_ceiling()
                  << " is_nodal=" << child->is_nodal()
                  << " parent_visits=" << root->get_info().visits
                  << "\n";
        if(info.visits == 0) continue;
        found_visited++;
        float score = test_uct(info.sum_reward, info.visits, root->get_info().visits);
        bool better = max_player ? (score > best_val) : (score < best_val);
        if(better) { best_val = score; bc = child; }
    }
    if(bc) {
        std::cerr << "best_child result: found (visits=" << bc->get_info().visits << ")\n";
        std::cerr << "Test 8 PASSED\n";
        return true;
    } else {
        std::cerr << "best_child result: nullptr! found_visited=" << found_visited << "\n";
        std::cerr << "Test 8 FAILED\n";
        return false;
    }
}
// =============================================================
//  main
// =============================================================
int main()
{
    bool ok = true;

    ok = test_startpos_state()          && ok;
    ok = test_fine_node_basics()        && ok;
    ok = test_rollout_isolation()       && ok;
    ok = test_full_mcts_pipeline()      && ok;
    ok = test_ceiling_ignition()         && ok;
    ok = test_expand_after_is_terminal() && ok;
    ok = test_engine_integration()      && ok;
    ok = test_best_child_debug()      && ok;

    std::cerr << "\n";
    if(ok)
    {
        std::cerr << "=== All tests PASSED ===\n";
        return 0;
    }
    else
    {
        std::cerr << "=== Some tests FAILED ===\n";
        return 1;
    }
}

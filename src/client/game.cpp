#include "game.h"
#include <regex>
#include <iostream>
#include <ranges>
#include <algorithm>
#include <cassert>
#include <optional>
#include <variant>
#include <array>
#include <string_view>
#include <stack>
#include "check_position.h"
#include "pgnparser.h"
#include "pgn_loading.h"
#include "hypercuboid.h"


std::string show_comments(std::vector<std::string> const &comments)
{
    std::string result;
    bool first = true;
    for (auto const &c : comments)
    {
        if (!first) result += ' ';
        result += '{' + c + '}';
        first = false;
    }
    return result;
}

game::game(std::unique_ptr<gnode<comments_t>> gt)
: gametree{std::move(gt)}, current_node{gametree.get()}, cached{}
{
    cached.push_back(std::make_pair(current_node->get_state(), std::optional<ext_move>()));
    now = cached.begin();
}

game game::from_pgn(std::string input, game::load_options options)
{
    auto ag = pgnparser(input).parse_game(options.initial_position_only);
    if(!ag.has_value())
        throw std::runtime_error("Bad input, parse failed");
    pgnparser_ast::gametree &gt_ast = ag->gt;
    auto [initial, error] = options.initial_position_only && options.allow_illegal_history
        ? pgn_loading::load_initial_position<true>(*ag)
        : pgn_loading::load_initial_position<false>(*ag);
    const bool illegal_setup = error.has_value();
    if(illegal_setup && !options.allow_illegal_history)
        throw *error;
    game g(gnode<comments_t>::create_root(std::move(initial), ag->comments, illegal_setup));
    if(ag->headers.contains("Promotions"))
        g.headers.emplace("Promotions", format_promotions(g.get_promotion_options()));
    for(const auto &[key, value] : ag->headers)
        g.set_header(key, value);
    if(options.initial_position_only)
        return g;
    using node_t = gnode<comments_t>;
    using ast_tree = pgnparser_ast::gametree;
    auto make_child = [&ag, options](node_t *node, const pgnparser_ast::actions &act_ast) {
        state s = node->get_state();
        std::vector<ext_move> moves;
        for(const auto &mv_ast: act_ast.moves)
        {
            moves.push_back(s.apply_move_ast(mv_ast, ag->source_text));
        }
        if(!s.test_present())
        {
            throw pgn_validation_error(
                pgn_validation_error::present_not_advanced{},
                pgn_validation_error::make_subject(act_ast, ag->source_text)
            );
        }
        s.submit<true>();
        bool illegal_history = node->has_illegal_history();
        if(!illegal_history)
        {
            const auto exposure = s.find_royal_exposure(false);
            illegal_history = exposure.has_value();
            if(illegal_history && !options.allow_illegal_history)
                throw pgn_validation_error(*exposure,
                    pgn_validation_error::make_subject(act_ast, ag->source_text));
        }
        action act = action::from_vector(moves, node->get_state());
        return node_t::create_child(
            node, std::move(s), std::move(act), act_ast.comments, illegal_history);
    };
    struct branch_frame {
        node_t *parent;
        const ast_tree::variations_t *variations;
        std::size_t next_child;
        std::unique_ptr<node_t> pending_child;
    };
    // DFS stack
    std::stack<branch_frame> branches;
    auto *node = g.gametree.get();
    const auto *tree = &gt_ast;
    while(true)
    {
        const auto *variations = std::get_if<ast_tree::variations_t>(&tree->variations_or_outcome);
        if(variations && !variations->empty())
        {
            const auto &[act_ast, child_tree] = variations->front();
            auto child = make_child(node, act_ast);
            auto *next = child.get();
            if(variations->size() == 1)
            {
                // Single continuations always start at a fresh node.
                assert(node->get_children().empty());
                node->add_child(std::move(child));
            }
            else
            {
                // Keep an incoming branch detached until fully validated.
                branches.push({node, variations, 1, std::move(child)});
            }
            node = next;
            tree = child_tree.get();
            continue;
        }

        // node is the last endpoint visited, including after a nested fork.
        while(!branches.empty())
        {
            auto &frame = branches.top();
            if(frame.parent->find_child(frame.pending_child->get_action()))
                frame.parent->delete_child(frame.pending_child->get_action());
            frame.parent->add_child(std::move(frame.pending_child));
            if(frame.next_child < frame.variations->size())
            {
                const auto &[act_ast, child_tree] = (*frame.variations)[frame.next_child++];
                frame.pending_child = make_child(frame.parent, act_ast);
                node = frame.pending_child.get();
                tree = child_tree.get();
                break;
            }
            branches.pop();
        }
        if(branches.empty()) break;
    }
    g.current_node = node;
    g.fresh();
    return g;
}

void game::fresh()
{
    discard_preview();
    nonterminal.reset();
    cached.clear();
    cached.push_back(std::make_pair(current_node->get_state(), std::optional<ext_move>()));
    now = cached.begin();
}

std::pair<int,bool> game::get_current_present() const
{
    return get_current_state().get_present().get();
}

std::vector<ext_move> game::get_cached_moves() const
{
    std::vector<ext_move> result;
    for(auto it = cached.begin()+1; it != now+1; ++it)
    {
        if(it->second)
        {
            result.push_back(*(it->second));
        }
    }
    return result;
}

const state &game::get_current_state() const
{
    // Position queries, including playability and move generation, see the preview.
    return preview ? *preview : now->first;
}

const state &game::get_unmoved_state() const
{
    return cached[0].first;
}

std::vector<boards_info_t> game::get_current_boards() const
{
    return get_current_state().get_boards();
}

std::pair<std::vector<boards_info_t>, std::vector<full_move>> game::get_phantom_boards_and_checks() const
{
    const state &s = get_current_state();
    const bool c = s.get_present().c();
    const state &ps = s.phantom();
    std::vector<full_move> checks;
    std::set<vec4> tls;
    for(full_move fm : ps.find_checks(!c))
    {
        checks.push_back(fm);
        tls.insert(fm.from.tl());
        // A check can travel to another timeline.  The UI needs the
        // destination board as well as the origin board in order to draw
        // the complete arrow when phantom boards are enabled.
        tls.insert(fm.to.tl());
    }
	//print_range("Phantom timelines: ", tls);
    std::vector<boards_info_t> all_boards = ps.get_boards();
    std::vector<boards_info_t> result;
    for(const auto &[l, t, color, fen] : all_boards)
    {
        if(tls.contains(vec4(0, 0, t, l)))
        {
            result.push_back({l, t, color, fen});
        }
    }
    return {result, checks};
}

std::tuple<std::vector<int>, std::vector<int>, std::vector<int>> game::get_current_timeline_status() const
{
    return get_current_state().get_timeline_status();
}

std::vector<vec4> game::gen_move_if_playable(vec4 p) const
{
    if(is_playable(p))
    {
        const state &cs = get_current_state();
        std::vector<vec4> result;
        for(vec4 v : cs.gen_piece_move(p))
        {
            result.push_back(v);
        }
        return result;
    }
    else
    {
        return std::vector<vec4>();
    }
}

game::match_status_t game::get_match_status() const
{
    if(has_illegal_history()) return match_status_t::ILLEGAL_HISTORY;
    const state &s = current_node->get_state();
    if(!nonterminal.has_value())
    {
        auto [w, ss] = HC_info::build_HC(s);
        nonterminal = w.search(ss).first().has_value();
    }
    if(*nonterminal)
    {
        return match_status_t::PLAYING;
    }
    const bool c = s.get_present().c();
    if(s.has_phantom_check())
    {
        return c ? match_status_t::WHITE_WINS : match_status_t::BLACK_WINS;
    }
    else
    {
        return match_status_t::STALEMATE;
    }
}

std::vector<vec4> game::get_movable_pieces() const
{
    state s = get_current_state();
    return s.get_movable_pieces();
}

bool game::is_playable(vec4 p) const
{
    const state &cs = get_current_state();
    auto [mandatory_timelines, optional_timelines, unplayable_timelines] = cs.get_timeline_status();
    if (std::find(mandatory_timelines.begin(), mandatory_timelines.end(), p.l()) != mandatory_timelines.end() ||
        std::find(optional_timelines.begin(), optional_timelines.end(), p.l()) != optional_timelines.end()) 
    {
        const bool c = cs.get_present().c();
        const turn v1{p.t(), c};
        auto v2 = cs.get_timeline_end(p.l());
        if(v1 == v2)
        {
            piece_t p_piece = cs.get_piece(p, c);
            if(p_piece != NO_PIECE && p_piece != WALL_PIECE)
            {
                return c == static_cast<int>(piece_color(p_piece));
            }
        }
    }
    return false;
}

bool game::can_undo() const
{
    return now != cached.begin();
}

bool game::can_redo() const
{
    return now+1 != cached.end();
}

bool game::can_submit(bool allow_submit_with_checks) const
{
    // Submission availability describes the real draft, even during a preview.
    return allow_submit_with_checks ? now->first.test_present()
        : now->first.can_submit().has_value();
}

bool game::undo()
{
    discard_preview();
    bool flag = can_undo();
    if(flag)
    {
        now--;
    }
    return flag;
}

bool game::redo()
{
    discard_preview();
    bool flag = can_redo();
    if(flag)
    {
        now++;
    }
    return flag;
}

bool game::apply_move(ext_move m)
{
    // A preview is never committed or used as the starting state of a real move.
    discard_preview();
    const auto normalized = now->first.normalize_promotion(m.fm, m.promote_to);
    if(!normalized)
    {
        return false;
    }
    std::optional<state> ans = now->first.can_apply(*normalized);
    if(ans)
    {
        state new_state = std::move(ans.value());
        cached.erase(now + 1, cached.end());
        cached.push_back(std::make_pair(new_state, normalized));
        now = cached.end() - 1;
        return true;
    }
    return false;
}

bool game::preview_move(ext_move m)
{
    // Replacing a preview always starts from the real draft.
    discard_preview();
    const auto normalized = now->first.normalize_promotion(m.fm, m.promote_to);
    if(!normalized)
        return false;
    preview = now->first.can_apply(*normalized);
    return preview.has_value();
}

void game::discard_preview()
{
    preview.reset();
}

std::optional<action> game::get_submit_action() const
{
    if(!can_submit(true)) return std::nullopt;
    return action::from_vector(get_cached_moves(), get_unmoved_state());
}

bool game::submit(bool allow_submit_with_checks)
{
    discard_preview();
    if(!now->first.test_present()) return false;
    state submitted = now->first;
    if(allow_submit_with_checks)
        submitted.submit<true>();
    else if(!submitted.submit<false>())
        return false;
    auto act = action::from_vector(get_cached_moves(), get_unmoved_state());
    auto *child = current_node->find_child(act);
    if(!child)
    {
        const bool illegal_history = has_illegal_history()
            || (allow_submit_with_checks && submitted.find_royal_exposure(false).has_value());
        child = current_node->add_child(gnode<comments_t>::create_child(
            current_node, std::move(submitted), std::move(act), comments_t{}, illegal_history));
    }
    current_node = child;
    fresh();
    return true;
}

bool game::has_child(const action &act) const
{
    return current_node->find_child(act) != nullptr;
}

bool game::has_illegal_history() const noexcept
{
    return current_node->has_illegal_history();
}

bool game::delete_child(const action &act)
{
    if(!current_node->find_child(act)) return false;
    discard_preview();
    return current_node->delete_child(act);
}

bool game::visit_child(const action &act)
{
    auto *child = current_node->find_child(act);
    if(!child) return false;
    current_node = child;
    fresh();
    return true;
}

bool game::currently_check() const
{
    const bool c = get_current_state().get_present().c();
    return get_current_state().find_checks(!c).first().has_value();
}

std::vector<std::pair<vec4, vec4>> game::get_current_checks() const
{
    const bool c = get_current_state().get_present().c();
    std::vector<std::pair<vec4, vec4>> result;
    for(full_move fm : get_current_state().find_checks(!c))
    {
        result.push_back(std::make_pair(fm.from, fm.to));
    }
    return result;
}

std::pair<int, int> game::get_board_size() const
{
    return get_current_state().get_board_size();
}

promotion_options game::get_promotion_options() const
{
    return gametree->get_stored_state().get_promotion_options();
}

std::optional<action> game::suggest_action()
{
    if(has_illegal_history()) return std::nullopt;
    if(nonterminal.has_value() && !*nonterminal)
        return std::nullopt;
    const state &s = current_node->get_state();
    auto [w, ss] = HC_info::build_HC(s);
    bool found_legal_action = false;
    for(moveseq mvs : w.search(ss))
    {
        found_legal_action = true;
        nonterminal = true;
        std::vector<ext_move> emvs;
        std::transform(mvs.begin(), mvs.end(), std::back_inserter(emvs), [&s](full_move m){
            return ext_move(m, s);
        });
        action act = action::from_vector(emvs, s);
        if(!current_node->find_child(act))
        {
            return act;
        }
    }
    nonterminal = found_legal_action;
    return std::nullopt;
}

bool game::add_child(action act)
{
    discard_preview();
    if(current_node->find_child(act))
    {
        return false;
    }
    state newstate = current_node->get_state();
    for(const auto &move : act.get_moves_view())
        if(!newstate.apply_move<false>(move)) return false;
    if(!newstate.test_present()) return false;
    if(has_illegal_history())
        newstate.submit<true>();
    else if(!newstate.submit<false>())
        return false;
    auto child = gnode<comments_t>::create_child(current_node, std::move(newstate), std::move(act), comments_t{});
    current_node->add_child(std::move(child));
    if(!has_illegal_history()) nonterminal = true;
    return true;
}

/////////////////////////////
// Comments and navigation //
/////////////////////////////

game::comments_t game::get_comments() const
{
    return current_node->get_info();
}

void game::set_comments(const comments_t &c) const
{
    current_node->set_info(c);
}

bool game::has_parent() const
{
    return current_node->get_parent() != nullptr;
}

void game::visit_parent()
{
    discard_preview();
    if(!has_parent())
        return;
    current_node = current_node->get_parent();
    fresh();
}

std::vector<game::action_summary> game::get_child_actions(pgn_options show_flags) const
{
    std::vector<action_summary> result;
    auto &children = current_node->get_children();
    state s = current_node->get_state();
    for(const auto &child : children)
    {
        const action &act = child->get_action();
        const auto flags = child->has_illegal_history()
            ? show_flags & ~(pgn_options::SHOW_MATE | pgn_options::SHOW_OUTCOME)
            : show_flags;
        std::string txt = act.pgn(s, flags);
        result.push_back({act, std::move(txt), child->has_illegal_history()});
    }
    return result;
}

std::vector<std::tuple<action, std::string>> game::get_historical_actions() const
{
    std::vector<std::tuple<action, std::string>> result;
    gnode<comments_t> *node = current_node;
    
    // Walk from current node back to root, collecting actions
    while(node->get_parent() != nullptr)
    {
        const action &act = node->get_action();
        // Pretty print using parent's state (state before the action was applied)
        std::string txt = act.pgn(node->get_parent()->get_state());
        result.push_back({act, txt});
        node = node->get_parent();
    }
    
    // Reverse since we built it backwards
    std::reverse(result.begin(), result.end());
    return result;
}

namespace {
constexpr std::array<std::string_view, 12> ordered_headers = {
    "Event", "Site", "Date", "Round", "White", "Black", "Result",
    "Variant", "Size", "Timeline", "Promotions", "Puzzle"
};

std::string_view known_header(std::string_view key)
{
    using pgnparser_ast::header_key_less;
    if(header_key_less::compare(key, "Board") == 0)
        return "Board";
    for(const auto name : ordered_headers)
    {
        if(header_key_less::compare(key, name) == 0)
            return name;
    }
    return {};
}

bool derived_header(std::string_view key)
{
    return key == "Size" || key == "Timeline" || key == "Promotions";
}
} /* anonymous namespace */

std::optional<std::string> game::get_header(const std::string &key) const
{
    const auto canonical = known_header(key);
    if(canonical == "Size")
    {
        const auto [x, y] = gametree->get_stored_state().get_board_size();
        return std::to_string(x) + "x" + std::to_string(y);
    }
    if(canonical == "Timeline")
    {
        const auto range = gametree->get_stored_state().get_initial_lines_range();
        return range == timeline_layout::even.initial_range() ? "Even" : "Odd";
    }
    if(canonical == "Promotions")
        return format_promotions(get_promotion_options());

    const auto it = headers.find(key);
    if(canonical == "Variant" && (it == headers.end() || it->second.empty()))
    {
        const auto board = headers.find("board");
        if(board != headers.end())
            return board->second;
    }
    if(it != headers.end())
        return it->second;
    return std::nullopt;
}

void game::set_header(const std::string &key, const std::string &value)
{
    const auto canonical = known_header(key);
    if(!derived_header(canonical))
        headers.insert_or_assign(canonical.empty() ? key : std::string(canonical), value);
}

std::vector<std::string> game::header_keys() const
{
    std::vector<std::string> keys;
    for(const auto name : ordered_headers)
    {
        if(get_header(std::string(name)).has_value())
            keys.emplace_back(name);
    }
    if(headers.contains("board"))
        keys.emplace_back("Board");
    for(const auto &[key, value] : headers)
    {
        if(known_header(key).empty())
            keys.push_back(key);
    }
    return keys;
}

std::string game::show_pgn(
    pgn_options show_flags,
    bool complete_game_tree,
    bool standard_metadata_only)
{
    std::ostringstream oss;
    const auto print_header = [&oss](std::string_view key, const std::string &value) {
        oss << '[' << key << " \"" << value << "\"]\n";
    };
    for(const auto key : ordered_headers)
    {
        if(key == "Variant")
        {
            if(const auto board = get_header("Board"))
            {
                print_header(standard_metadata_only ? "Variant" : "Board", *board);
                continue;
            }
        }
        if(key == "Promotions" && !headers.contains("Promotions")
            && get_promotion_options() == promotion_options::QUEEN)
            continue;
        if(const auto value = get_header(std::string(key)))
            print_header(key, *value);
    }
    if(!standard_metadata_only)
    {
        for(const auto &[key, value] : headers)
        {
            if(known_header(key).empty())
                print_header(key, value);
        }
    }
    auto result = std::move(oss).str();
    oss = std::ostringstream{};
    oss << gametree->get_state().show_fen();
    if(complete_game_tree)
    {
        oss << gametree->pgn_tree(show_comments, show_flags);
    }
    else
    {
        oss << current_node->pgn_path(show_comments, show_flags);
    }
    oss << "\n";
    /* indent by 1 space * number of nested parentheses
    do not indent inside comments */
    int parentheses_level = 0;
    int curly_brace_level = 0;
    for(char c : oss.str())
    {
        result += c;
        if(c == '(')
        {
            parentheses_level++;
        }
        else if(c == ')')
        {
            parentheses_level--;
        }
        else if(c == '{')
        {
            curly_brace_level++;
        }
        else if(c == '}')
        {
            curly_brace_level--;
        }
        else
        {
            if(curly_brace_level == 0 && c == '\n')
            {
                result += std::string(parentheses_level, ' ');
            }
        }
    }
    return result;
}

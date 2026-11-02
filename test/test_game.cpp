#undef NDEBUG
#include <cassert>
#include <type_traits>
#include <utility>
#include "state.h"
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include "gametree.h"
#include "hypercuboid.h"
#include "pgnparser.h"
#include <algorithm>
#include <cstdint>
#include <set>
#include <sstream>
#include <vector>
#include "core/hash.h"
#include "game.h"
#include "board.h"

namespace multiverse_tests
{
static_assert(!std::is_polymorphic_v<multiverse>);
static_assert(std::is_same_v<decltype(std::declval<const multiverse &>().get_board(0, 1, false)),
                             std::shared_ptr<const board>>);
static_assert(std::is_same_v<decltype(std::declval<const state &>().get_board(0, 1, false)),
                             std::shared_ptr<const board>>);

static_assert(std::is_nothrow_move_constructible_v<multiverse>);
static_assert(std::is_nothrow_move_assignable_v<multiverse>);
static_assert(std::is_nothrow_swappable_v<multiverse>);
static_assert(std::is_nothrow_constructible_v<state, multiverse &&>);
static_assert(!std::is_nothrow_constructible_v<state, const multiverse &>);
static_assert(std::is_nothrow_move_constructible_v<state>);
static_assert(std::is_nothrow_swappable_v<state>);

namespace
{
const std::string empty = "8/8/8/8/8/8/8/8";

void test_layout()
{
    constexpr auto odd = timeline_layout::odd;
    constexpr auto even = timeline_layout::even;
    static_assert(odd == timeline_layout::odd);
    static_assert(even == timeline_layout::even);
    static_assert(odd != even);
    assert(!odd.is_even() && even.is_even());
    assert((odd.initial_range() == std::pair{0, 0}));
    assert((even.initial_range() == std::pair{-1, 0}));
    assert(odd.pretty_l(-1) == "-1");
    assert(odd.pretty_l(0) == "0");
    assert(even.pretty_l(-1) == "-0");
    assert(even.pretty_l(0) == "+0");
    assert(even.pretty_l(-2) == "-1");
    assert(even.pretty_l(1) == "+1");
    for(int l = vec4::L_MIN; l <= vec4::L_MAX; ++l)
    {
        assert(odd.mirror(odd.mirror(l)) == l);
        assert(even.mirror(even.mirror(l)) == l);
    }
}

void test_activation(timeline_layout layout)
{
    std::vector<boards_info_t> initial{{0, 3, false, empty}};
    if(layout.is_even())
        initial.emplace_back(-1, 3, false, empty);
    multiverse m(layout, initial);
    const auto central = layout.initial_range();
    assert(m.get_active_range() == central);
    assert((m.get_present() == turn{3, false}));
    assert(m.pretty_lt(vec4(0, 0, 3, central.first))
           == (layout.is_even() ? "(-0T3)" : "(0T3)"));

    m.insert_board(1, 2, true, std::make_shared<board>(empty));
    assert((m.get_active_range() == std::pair{central.first, 1}));
    assert((m.get_present() == turn{2, true}));
    m.insert_board(2, 1, false, std::make_shared<board>(empty));
    assert((m.get_active_range() == std::pair{central.first, 1}));
    // An inactive historical board must not move the present yet.
    assert((m.get_present() == turn{2, true}));
    m.insert_board(layout.mirror(1), 3, false, std::make_shared<board>(empty));
    assert((m.get_active_range() == std::pair{layout.mirror(1), 2}));
    assert((m.get_present() == turn{1, false}));
    m.insert_board(3, 3, false, std::make_shared<board>(empty));
    assert((m.get_active_range() == std::pair{layout.mirror(1), 2}));
    m.insert_board(layout.mirror(2), 3, false, std::make_shared<board>(empty));
    assert((m.get_active_range() == std::pair{layout.mirror(2), 3}));

    // Construction from the same boards must initialize the same cached ranges.
    const multiverse rebuilt(layout, m.get_boards<true>());
    assert(rebuilt.get_lines_range() == m.get_lines_range());
    assert(rebuilt.get_active_range() == m.get_active_range());
    assert(rebuilt.get_present() == m.get_present());
}

void test_value_ownership()
{
    const multiverse original(timeline_layout::odd, {{0, 1, false, "k3/4/4/K*3"}}, 4, 4);
    state copied(original, promotion_options::NONE);
    assert(copied.get_board_ptr(0, 1, false) == original.get_board_ptr(0, 1, false));
    state independent = copied;
    const auto original_boards = copied.get_boards();
    assert(independent.apply_move<true>(full_move(vec4(0, 0, 1, 0), vec4(1, 0, 1, 0))));
    assert(copied.get_boards() == original_boards);
    assert((original.get_timeline_end(0) == turn{1, false}));
    assert((independent.get_timeline_end(0) == turn{1, true}));

    multiverse movable = original;
    const board *shared = movable.get_board_ptr(0, 1, false);
    state moved(std::move(movable), promotion_options::NONE);
    assert(moved.get_boards() == copied.get_boards());
    assert(moved.get_board_ptr(0, 1, false) == shared);
    assert(moved.get_promotion_options() == promotion_options::NONE);

    state other(multiverse(timeline_layout::even,
                          {{-1, 2, true, empty}, {0, 2, true, empty}}));
    const auto other_boards = other.get_boards();
    swap(moved, other);
    assert((moved.get_board_size() == std::pair{8, 8}));
    assert((moved.get_initial_lines_range() == std::pair{-1, 0}));
    assert((moved.get_present() == turn{2, true}));
    assert(moved.get_boards() == other_boards);
    assert((other.get_board_size() == std::pair{4, 4}));
    assert((other.get_initial_lines_range() == std::pair{0, 0}));
    assert(other.get_boards() == original_boards);
    assert(other.get_promotion_options() == promotion_options::NONE);

    state transferred(std::move(other));
    moved = std::move(transferred);
    assert(moved.get_boards() == original_boards);
    assert((moved.get_board_size() == std::pair{4, 4}));
    assert((moved.get_initial_lines_range() == std::pair{0, 0}));
}
} /* anonymous namespace */

int run()
{
    test_layout();
    test_activation(timeline_layout::odd);
    test_activation(timeline_layout::even);
    test_value_ownership();
    return 0;
}
} /* namespace multiverse_tests */

namespace gametree_tests
{
namespace {

struct counted_info
{
    int value;
    inline static int copies = 0;

    explicit counted_info(int value) : value(value) {}
    counted_info(const counted_info &other) : value(other.value) { ++copies; }
    counted_info(counted_info &&) = default;
};

void test_states_and_attachment(const state &initial, const action &act)
{
    using node_t = gnode<std::string>;
    auto root = node_t::create_root(initial, "root");
    auto *child = root->add_child(node_t::create_child(root.get(), std::nullopt, act, "child"));
    auto expected = initial.can_apply(act);
    assert(expected);
    assert(child->get_state().get_boards() == expected->get_boards());
    assert(child->get_stored_state().get_boards() == expected->get_boards());

    // The black king cannot move onto its own pawn on e7.
    const auto invalid_act = action::from_moveseq(
        {full_move(vec4{4, 7, 1, 0}, vec4{4, 6, 1, 0})}, *expected);
    assert(!expected->can_apply(invalid_act));
    auto *invalid = child->add_child(node_t::create_child(child, std::nullopt, invalid_act, "invalid"));
    for(int attempt = 0; attempt < 2; ++attempt)
    {
        bool rejected = false;
        try { invalid->get_state(); }
        catch(const std::runtime_error &error)
        {
            rejected = std::string(error.what()).find("Stored action is invalid") != std::string::npos;
        }
        assert(rejected);
        bool uncached = false;
        try { invalid->get_stored_state(); }
        catch(const std::bad_optional_access &) { uncached = true; }
        assert(uncached);
    }

    const auto child_count = root->get_children().size();
    bool null_rejected = false;
    try { root->add_child(nullptr); }
    catch(const std::invalid_argument &) { null_rejected = true; }
    assert(null_rejected);
    auto other_root = node_t::create_root(initial, "other");
    bool parent_rejected = false;
    try { root->add_child(node_t::create_child(other_root.get(), std::nullopt, act, "wrong parent")); }
    catch(const std::invalid_argument &) { parent_rejected = true; }
    assert(parent_rejected);
    assert(root->get_children().size() == child_count);

    auto copy = root->clone();
    auto *copied_child = copy->get_children().front().get();
    assert(copied_child != child && copied_child->get_parent() == copy.get());
    assert(copied_child->get_children().front()->get_parent() == copied_child);
    assert(copied_child->get_state().get_boards() == expected->get_boards());
    copied_child->set_info("changed");
    assert(child->get_info() == "child");
}

void test_child_index(const state &initial, const action &act, const action &other)
{
    using node_t = gnode<std::string>;
    auto root = node_t::create_root(initial, "root");
    assert(!root->find_child(act));
    auto *first = root->add_child(node_t::create_child(root.get(), std::nullopt, act, "first"));
    assert(!root->add_child(node_t::create_child(root.get(), std::nullopt, act, "duplicate")));
    assert(root->get_children().size() == 1);
    assert(first->get_info() == "first");
    auto *second = root->add_child(node_t::create_child(root.get(), std::nullopt, other, "second"));
    assert(root->find_child(act) == first);
    assert(std::as_const(*root).find_child(act) == first);
    auto copy = root->clone();
    assert(copy->find_child(act) == copy->get_children().front().get());
    assert(copy->find_child(act) != first);
    assert(root->delete_child(act));
    assert(!root->find_child(act));
    assert(root->find_child(other) == second);
    assert(root->get_children().front().get() == second);
    assert(!root->delete_child(act));
    auto *replacement = root->add_child(node_t::create_child(root.get(), std::nullopt, act, "replacement"));
    assert(root->find_child(act) == replacement);
    assert(copy->find_child(act)->get_info() == "first");
    copy.reset();
    assert(root->find_child(act) == replacement);
}

void test_owned_payload_and_formatting(const state &initial, const action &act, const action &other)
{
    auto owned = gnode<std::unique_ptr<int>>::create_root(initial, std::make_unique<int>(7));
    assert(*owned->get_info() == 7);
    using node_t = gnode<counted_info>;
    auto root = node_t::create_root(initial, counted_info{1});
    auto *child = root->add_child(node_t::create_child(root.get(), std::nullopt, act, counted_info{2}));
    root->add_child(node_t::create_child(root.get(), std::nullopt, other, counted_info{3}));
    const std::function<std::string(const counted_info &)> show =
        [marker = counted_info{0}](const counted_info &info) {
            return "{" + std::to_string(info.value + marker.value) + "}";
        };
    counted_info::copies = 0;
    const auto tree = root->pgn_tree(show, pgn_options::SHOW_NOTHING);
    const auto path = child->pgn_path(show, pgn_options::SHOW_NOTHING);
    assert(tree.find("{1}") != std::string::npos);
    assert(tree.find("{2}") != std::string::npos);
    assert(tree.find("{3}") != std::string::npos);
    assert(path.find("{2}") != std::string::npos);
    assert(counted_info::copies == 0);
}

} /* anonymous namespace */

int run() try
{
    state initial = state::from_pgn<true>("[Variant \"Standard\"]");
    auto [info, space] = HC_info::build_HC(initial);
    std::vector<action> actions;
    for(const auto &moves : info.search<natural_HC_ordering, hc_policy::balanced>(std::move(space)))
    {
        actions.push_back(action::from_moveseq(moves, initial));
        if(actions.size() == 2) break;
    }
    assert(actions.size() == 2);
    test_states_and_attachment(initial, actions[0]);
    test_owned_payload_and_formatting(initial, actions[0], actions[1]);
    test_child_index(initial, actions[0], actions[1]);
    return 0;
}

catch(const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
} /* namespace gametree_tests */

namespace hash_tests
{
namespace
{
void test_boards()
{
    const board empty("8/8/8/8/8/8/8/8");
    const board copy = empty;
    // Version-1 reference value, evaluated independently with integer
    // arithmetic, fixes the output across platforms and future builds.
    assert(hash_board(empty) == UINT64_C(0x3eda1467921a3b26));
    assert(hash_board(empty) == hash_board(copy));
    assert(hash_board(empty) == hash_board(board("8/8/8/8/8/8/8/8")));

    const board unmoved("8/8/8/8/8/8/8/R*K*P*W*4");
    const board moved("8/8/8/8/8/8/8/RKPW4");
    assert(unmoved.get_fen() == moved.get_fen());
    assert(hash_board(unmoved) != hash_board(moved));

    // Exercise every piece family, color, and square independently. This
    // catches omitted masks and field/coordinate combinations that collapse.
    const std::vector<piece_t> pieces{
        WALL_PIECE, KING_W, COMMON_KING_W, ROYAL_QUEEN_W,
        QUEEN_W, PRINCESS_W, ROOK_W, BISHOP_W,
        UNICORN_W, DRAGON_W, KNIGHT_W, PAWN_W, BRAWN_W,
        KING_B, COMMON_KING_B, ROYAL_QUEEN_B,
        QUEEN_B, PRINCESS_B, ROOK_B, BISHOP_B,
        UNICORN_B, DRAGON_B, KNIGHT_B, PAWN_B, BRAWN_B
    };
    std::set<std::uint64_t> hashes{hash_board(empty)};
    for(piece_t piece : pieces)
    {
        for(int square = 0; square < BOARD_SIZE; ++square)
        {
            board b = empty;
            b.set_piece(square, piece);
            assert(hashes.insert(hash_board(b)).second);
        }
    }

    board mutable_board = empty;
    const auto before = hash_board(mutable_board);
    mutable_board.set_piece(ppos(0, 0), QUEEN_W);
    assert(hash_board(mutable_board) != before);
    mutable_board.set_piece(ppos(0, 0), NO_PIECE);
    assert(hash_board(mutable_board) == before);
}

void test_multiverses()
{
    const std::string empty = "8/8/8/8/8/8/8/8";
    std::vector<boards_info_t> boards{
        {-1, 2, false, empty}, {-1, 2, true, empty},
        {0, 1, true, empty}, {0, 2, false, empty},
        {1, 2, false, empty}
    };
    multiverse original(timeline_layout::odd, boards);
    const auto original_hash = hash_multiverse(original);
    assert(hash_multiverse(multiverse(original)) == original_hash);
    std::reverse(boards.begin(), boards.end());
    const multiverse reordered(timeline_layout::odd, boards);
    assert(hash_multiverse(reordered) == original_hash);

    // Equal endpoint boards do not make different histories duplicates.
    auto changed_boards = boards;
    for(auto &[line, time, color, fen] : changed_boards)
        if(line == 0 && time == 1 && color)
            fen = "8/8/8/8/8/8/8/R7";
    const multiverse changed(timeline_layout::odd, changed_boards);
    assert(hash_board(*changed.get_board(0, 2, false)) ==
           hash_board(*reordered.get_board(0, 2, false)));
    assert(hash_multiverse(changed) != original_hash);
    assert(hash_multiverse(original) == original_hash);

    const multiverse shifted(timeline_layout::odd, {{0, 3, false, empty}});
    const multiverse earlier(timeline_layout::odd, {{0, 2, false, empty}});
    const multiverse other_color(timeline_layout::odd, {{0, 2, true, empty}});
    assert(hash_multiverse(shifted) != hash_multiverse(earlier));
    assert(hash_multiverse(other_color) != hash_multiverse(earlier));

    const multiverse odd(timeline_layout::odd, boards);
    const multiverse even(timeline_layout::even, boards);
    assert(hash_multiverse(odd) != hash_multiverse(even));

    // A 4x4 board and an 8x8 board with matching walls have equal contents,
    // irrespective of nominal dimensions.
    const multiverse small(timeline_layout::odd, {{0, 1, false, "3k/4/4/K3"}}, 4, 4);
    const multiverse square(timeline_layout::odd,
        {{0, 1, false, "########/########/########/########/3k####/4####/4####/K3####"}});
    assert(hash_board(*square.get_board(0, 1, false)) ==
           hash_board(*small.get_board(0, 1, false)));
    assert(hash_multiverse(square) == hash_multiverse(small));
    assert(hash_state(state(square)) == hash_state(state(small)));

    auto extended = earlier;
    extended.append_board(0, std::make_shared<board>(empty));
    assert(hash_multiverse(extended) != hash_multiverse(earlier));

    multiverse inactive(timeline_layout::odd, {{0, 2, false, empty},
                             {1, 2, false, empty},
                             {2, 2, false, empty}});
    assert(inactive.get_active_range().second == 1);
    const auto inactive_hash = hash_multiverse(inactive);
    const multiverse changed_inactive(timeline_layout::odd, {
        {0, 2, false, empty}, {1, 2, false, empty},
        {2, 2, false, "7r/8/8/8/8/8/8/8"}
    });
    assert(hash_multiverse(changed_inactive) != inactive_hash);
}

void test_states()
{
    game original = game::from_pgn("[Variant \"Standard - Turn Zero\"]\n1. Nf3 / Nf6");
    game annotated = game::from_pgn(R"(
[Event "Another event"]
[Result "0-1"]
[Variant "Standard - Turn Zero"]
1. Ng1f3 / Ng8f6 {recorded:black wins}
)");
    const state &position = original.get_current_state();
    assert(hash_state(position) == hash_state(annotated.get_current_state()));
    assert(hash_state(position) == hash_state(state(position)));

    multiverse universe(timeline_layout::odd, {{0, 1, false, "8/8/8/8/8/8/8/8"}});
    const state queen(universe, promotion_options::QUEEN);
    const state knight(universe, promotion_options::KNIGHT);
    assert(hash_state(queen) != hash_state(knight));

    // Before and after submission, the multiverse is identical but the stored
    // present/player changes. Rebuilding before submission uses the apparent
    // present rather than the inherited one, which must affect the state hash.
    const auto source = position.get_board(0, 2, false);
    const auto source_hash = hash_board(*source);
    state temporary = position;
    assert(temporary.apply_move(full_move("(0T2)e2e4")));
    game submitted = game::from_pgn("[Variant \"Standard - Turn Zero\"]\n1. Nf3 / Nf6\n2. e4");
    const state &rebuilt = submitted.get_current_state();
    assert(temporary.get_present() != rebuilt.get_present());
    assert(hash_state(temporary) != hash_state(rebuilt));
    const auto before_submit = hash_state(temporary);
    const auto boards_before_submit = temporary.show_fen();
    assert(temporary.submit());
    assert(temporary.show_fen() == boards_before_submit);
    assert(hash_state(temporary) != before_submit);
    assert(hash_state(temporary) == hash_state(rebuilt));

    // Moves append replacement boards while copied states keep shared history.
    assert(temporary.get_board(0, 2, false) == source);
    assert(hash_board(*source) == source_hash);
    const auto edited = source->replace_piece(ppos(0, 0), QUEEN_W);
    assert(edited != source);
    assert(hash_board(*edited) != source_hash);
    assert(hash_board(*position.get_board(0, 2, false)) == source_hash);

    // Phantom states retain their stored present despite advancing the boards.
    const state phantom = queen.phantom();
    multiverse phantom_universe = universe;
    phantom_universe.append_board(0, queen.get_board(0, 1, false));
    const state apparent(phantom_universe, promotion_options::QUEEN);
    assert(phantom.show_fen() == apparent.show_fen());
    assert(phantom.get_present() != apparent.get_present());
    assert(hash_state(phantom) != hash_state(apparent));
}
} /* anonymous namespace */

int run()
{
    test_boards();
    test_multiverses();
    test_states();
    return 0;
}
} /* namespace hash_tests */

namespace preview_tests
{
int run()
{
    game g = game::from_pgn("[Variant \"Standard - Turn Zero\"]");
    const auto [t, c] = g.get_current_present();
    assert(!c);
    const ext_move first(vec4(4, 1, t, 0), vec4(4, 3, t, 0), NO_PIECE);
    const ext_move second(vec4(3, 1, t, 0), vec4(3, 3, t, 0), NO_PIECE);
    const ext_move invalid(vec4(4, 1, t, 0), vec4(4, 6, t, 0), NO_PIECE);
    const state base = g.get_current_state();
    const auto pgn = g.show_pgn();
    const auto legal = g.gen_move_if_playable(first.get_from());
    const bool submit = g.can_submit();
    assert(g.is_playable(first.get_from()));
    assert(!legal.empty());
    assert(!submit);

    assert(g.preview_move(first));
    const auto expected = base.can_apply(first);
    assert(expected);
    assert(g.get_current_boards() == expected->get_boards());
    assert(g.get_current_boards() != base.get_boards());
    assert(g.get_cached_moves().empty());
    assert(!g.can_undo() && !g.can_redo());
    assert(expected->can_submit().has_value());
    assert(g.can_submit() == submit);
    // Position queries see the preview, while submission still sees the real draft.
    assert(!g.is_playable(first.get_from()));
    assert(g.gen_move_if_playable(first.get_from()).empty());
    assert(g.get_movable_pieces() == expected->get_movable_pieces());
    assert(g.get_current_present() == expected->get_present().get());
    assert(g.get_current_timeline_status() == expected->get_timeline_status());
    assert(g.show_pgn() == pgn);
    // Replacement calculates from the real position, not the previous preview.
    assert(g.preview_move(second));
    assert(g.get_current_boards() == base.can_apply(second)->get_boards());
    assert(!g.preview_move(invalid));
    assert(g.get_current_boards() == base.get_boards());
    assert(g.is_playable(first.get_from()));
    assert(g.gen_move_if_playable(first.get_from()) == legal);
    assert(g.preview_move(first));
    g.discard_preview();
    g.discard_preview();
    assert(g.get_current_boards() == base.get_boards());
    assert(g.gen_move_if_playable(first.get_from()) == legal);

    // Applying a different move discards the preview and starts from the real draft.
    assert(g.preview_move(first));
    assert(g.apply_move(second));
    assert(g.get_current_boards() == base.can_apply(second)->get_boards());
    assert(g.get_cached_moves() == std::vector<ext_move>{second});
    assert(g.undo());
    assert(g.get_current_boards() == base.get_boards());

    assert(g.apply_move(first));
    const auto applied = g.get_current_boards();
    assert(g.undo());
    assert(g.can_redo());
    assert(g.preview_move(second));
    g.discard_preview();
    assert(g.can_redo());
    assert(g.redo());
    assert(g.get_current_boards() == applied);
    assert(g.undo());
    assert(g.preview_move(second));
    assert(g.redo()); // Redo discards the preview and restores the original continuation.
    assert(g.get_current_boards() == applied);
    assert(g.undo());

    assert(g.preview_move(first));
    assert(!g.undo()); // Failed operations also discard previews.
    assert(g.get_current_boards() == base.get_boards());
    assert(g.preview_move(first));
    assert(!g.apply_move(invalid));
    assert(g.get_current_boards() == base.get_boards());
    assert(g.preview_move(first));
    g.visit_parent(); // No parent at the root.
    assert(g.get_current_boards() == base.get_boards());
    assert(g.preview_move(first));
    assert(g.submit() == submit);
    assert(g.get_current_boards() == base.get_boards());

    assert(g.preview_move(first));
    assert(g.apply_move(first));
    assert(g.get_current_boards() == applied);
    assert(g.get_cached_moves().size() == 1);
    assert(g.submit());
    const action continuation = std::get<0>(g.get_historical_actions().back());
    const auto [black_t, black_c] = g.get_current_present();
    assert(black_c);
    const ext_move black(vec4(4, 6, black_t, 0), vec4(4, 4, black_t, 0), NO_PIECE);
    assert(g.preview_move(black));
    g.visit_parent();
    assert(g.get_current_boards() == base.get_boards());
    assert(g.preview_move(second));
    assert(g.visit_child(continuation));
    assert(g.get_current_boards() == applied);

    game branching = game::from_pgn("[Variant \"Standard - Turn Zero\"]\n1. Nf3 / Nf6\n2. Nc3 / Nc6");
    const ext_move travel(vec4(5, 2, 3, 0), vec4(5, 4, 2, 0), NO_PIECE);
    const auto before_branch = branching.get_current_boards();
    assert(branching.preview_move(travel));
    const auto branch_preview = branching.get_current_boards();
    assert(branch_preview.size() > before_branch.size());
    branching.discard_preview();
    assert(branching.get_current_boards() == before_branch);
    assert(branching.apply_move(travel));
    assert(branching.get_current_boards() == branch_preview);

    game promotion = game::from_pgn("[Timeline \"odd\"]\n[Size \"8x8\"]\n[7k/P7/8/8/8/8/8/K7:0:1:w]");
    const ext_move promote(vec4(0, 6, 1, 0), vec4(0, 7, 1, 0), NO_PIECE);
    assert(promotion.preview_move(promote));
    const auto promoted = promotion.get_current_boards();
    assert(promotion.apply_move(promote));
    assert(promotion.get_current_boards() == promoted);
    return 0;
}
} /* namespace preview_tests */

namespace mate_tests
{
int run()
{
    const game stalemate = game::from_pgn(R"(
[Board "Custom"]
[Mode "5D"]
[k7/pn6/K7/8/8/8/6PB/8:0:1:w]
{
Expected result: Stalemate
Thanks @narik for providing this position
}
1. Bg1 / Kb8
2. Bd4 / Kc8
3. Bc3 / (0T3)Nb7>>(0T1)c7~
4. (0T4)Bc3>>(0T1)c6~ / (1T1)Ka8>>(0T1)b8~
5. (-2T2)Bxa7 (-1T2)Ka6>>(0T3)b5~ (1T2)Bc6>>(0T1)c6~
)");
    assert(stalemate.get_current_state().get_mate_type() == mate_type::STALEMATE);
    return 0;
}
} /* namespace mate_tests */

namespace import_deduplication_tests
{
int run()
{
    const std::string header = "[Variant \"Standard - Turn Zero\"]\n";
    game g = game::from_pgn(header + R"(
(1. Nf3 {old branch} / Nf6 {old reply} 2. Nc3 {old descendant})
(1. e4 {unrelated} / e5 {unrelated reply})
(1. Ng1f3 {new branch} / d5 {new reply})
1. d4 {main}
)");
    assert(g.get_comments() == std::vector<std::string>{"main"});
    g.visit_parent();
    const auto children = g.get_child_actions();
    assert(children.size() == 3);
    const auto e4 = std::get<0>(game::from_pgn(header + "1. e4").get_historical_actions().front());
    const auto nf3 = std::get<0>(game::from_pgn(header + "1. Nf3").get_historical_actions().front());
    const auto d4 = std::get<0>(game::from_pgn(header + "1. d4").get_historical_actions().front());
    assert(children[0].act == e4);
    assert(children[1].act == nf3);
    assert(children[2].act == d4);
    assert(!g.add_child(nf3));
    assert(g.visit_child(nf3));
    assert(g.get_comments() == std::vector<std::string>{"new branch"});
    assert(g.get_child_actions().size() == 1);
    assert(g.visit_child(g.get_child_actions().front().act));
    assert(g.get_comments() == std::vector<std::string>{"new reply"});
    assert(g.get_child_actions().empty());
    const auto exported = g.show_pgn();
    assert(exported.find("old branch") == std::string::npos);
    assert(exported.find("old reply") == std::string::npos);
    assert(exported.find("old descendant") == std::string::npos);
    assert(exported.find("unrelated reply") != std::string::npos);
    game roundtrip = game::from_pgn(exported);
    while(roundtrip.has_parent()) roundtrip.visit_parent();
    assert(roundtrip.get_child_actions().size() == 3);
    assert(roundtrip.visit_child(nf3));
    assert(roundtrip.get_comments() == std::vector<std::string>{"new branch"});

    // A final replacement with no descendants must become the loaded endpoint,
    // rather than retaining a pointer to the discarded branch's deepest node.
    game final = game::from_pgn(header + R"(
(1. Nf3 {discarded} / Nf6 2. Nc3)
1. Ng1f3 {survivor}
)");
    assert(final.get_historical_actions().size() == 1);
    assert(final.get_comments() == std::vector<std::string>{"survivor"});
    assert(final.get_child_actions().empty());
    final.visit_parent();
    assert(final.get_child_actions().size() == 1);
    assert(final.visit_child(nf3));

    game nested = game::from_pgn(header + R"(
1. Nf3
(1b. Nf6 {discarded reply} 2. Nc3)
1b. Ng8f6 {surviving reply}
)");
    assert(nested.get_comments() == std::vector<std::string>{"surviving reply"});
    assert(nested.get_historical_actions().size() == 2);
    assert(nested.get_child_actions().empty());
    nested.visit_parent();
    assert(nested.get_child_actions().size() == 1);

    // Reordered moves on independent timelines canonicalize to the same action.
    game reordered = game::from_pgn(R"(
[Timeline "Even"]
[Size "8x8"]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:+0:1:w]
[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:-0:1:w]
(1. (+0T1)e2e3 (-0T1)Ng1f3 {earlier order})
1. (-0T1)Ng1f3 (+0T1)e2e3 {later order}
)");
    assert(reordered.get_comments() == std::vector<std::string>{"later order"});
    reordered.visit_parent();
    assert(reordered.get_child_actions().size() == 1);

    const auto before_failed_load = final.show_pgn();
    bool rejected = false;
    try
    {
        final = game::from_pgn(header + "(1. Nf3 / Nf6) 1. Nf3 / e7e3");
    }
    catch(const pgn_validation_error &) { rejected = true; }
    assert(rejected);
    assert(final.show_pgn() == before_failed_load);
    return 0;
}
} /* namespace import_deduplication_tests */

namespace deletion_tests
{
int run()
{
    game g = game::from_pgn("[Variant \"Standard - Turn Zero\"]\n1. Nf3 {branch} / Nf6 {descendant}");
    const auto branch = std::get<0>(g.get_historical_actions().front());
    g.visit_parent();
    g.visit_parent();
    game alternative = game::from_pgn("[Variant \"Standard - Turn Zero\"]\n1. e4");
    const auto other = std::get<0>(alternative.get_historical_actions().front());
    assert(g.has_child(branch));
    assert(!g.has_child(other));
    assert(!g.visit_child(other));
    assert(g.add_child(other));
    assert(g.has_child(other));
    assert(!g.add_child(other));
    assert(g.get_historical_actions().empty());
    const auto invalid = action::from_vector(
        {ext_move(full_move("(0T1)e2e8"), NO_PIECE)}, g.get_unmoved_state());
    assert(!g.has_child(invalid));
    assert(!g.visit_child(invalid));
    assert(!g.add_child(invalid));
    const auto before = g.get_current_boards();
    const auto status = g.get_match_status();
    const auto move = other.get_moves_view().front();
    assert(g.apply_move(move));
    assert(g.get_submit_action() == other);
    assert(g.undo());
    assert(g.can_redo());
    assert(!g.get_submit_action()); // The redo tail is not part of the submitted action.
    assert(g.preview_move(move));
    assert(g.delete_child(branch));
    assert(g.get_current_boards() == before);
    assert(g.get_cached_moves().empty());
    assert(g.can_redo());
    assert(g.get_match_status() == status);
    assert(!g.delete_child(branch));
    assert(!g.has_child(branch));
    assert(!g.visit_child(branch));
    assert(g.get_child_actions().size() == 1);
    const auto exported = g.show_pgn();
    assert(exported.find("branch") == std::string::npos);
    assert(exported.find("descendant") == std::string::npos);
    assert(g.redo());
    const auto draft = g.get_cached_moves();
    assert(g.delete_child(other));
    assert(g.get_cached_moves() == draft && g.can_undo());
    assert(g.get_child_actions().empty());
    assert(g.submit()); // Deletion does not prevent creating the move again later.
    g.set_comments({"preserved"});
    g.visit_parent();
    assert(g.apply_move(move));
    assert(g.submit());
    assert(g.get_comments() == std::vector<std::string>{"preserved"});

    // Owning subtree removal releases descendant payloads and retains sibling order.
    using node = gnode<std::shared_ptr<int>>;
    auto root = node::create_root(g.get_unmoved_state(), nullptr);
    auto payload = std::make_shared<int>(7);
    std::weak_ptr<int> lifetime = payload;
    auto *child = root->add_child(node::create_child(root.get(), std::nullopt, branch, nullptr));
    child->add_child(node::create_child(child, std::nullopt, other, std::move(payload)));
    auto *sibling = root->add_child(node::create_child(root.get(), std::nullopt, other, nullptr));
    assert(root->delete_child(branch));
    assert(lifetime.expired());
    assert(root->get_children().size() == 1 && root->get_children().front().get() == sibling);
    assert(!root->delete_child(branch));
    assert(root->delete_child(other) && root->get_children().empty());
    return 0;
}
} /* namespace deletion_tests */

namespace fen_tests
{
namespace
{
void test_fen_error_messages_and_ranges()
{
    using error = board_fen_error;
    const board_fen_error short_rank(error::rank_too_short{{8, 7}}, "8/1dDDdD1/8", 2, 9);
    assert(std::holds_alternative<error::rank_too_short>(short_rank.get_error_type()));
    assert(short_rank.get_fen() == "8/1dDDdD1/8");
    assert(short_rank.get_start() == 2 && short_rank.get_end() == 9);
    assert(short_rank.get_range() == "1dDDdD1");
    const auto &shape = std::get<error::rank_too_short>(short_rank.get_error_type());
    assert(shape.expected == 8 && shape.actual == 7);
    assert(std::string(short_rank.what()) == "FEN rank has 7 squares; expected 8.");

    const board_fen_error long_rank(error::rank_too_long{{8, 9}}, "9", 0, 1);
    assert(std::string(long_rank.what()) == "FEN rank has 9 squares; expected 8.");
    const board_fen_error missing(error::too_few_ranks{{8, 1}}, "8", 1, 1);
    assert(missing.get_range().empty());
    assert(std::string(missing.what()) == "FEN rank count is 1; expected 8.");
    const board_fen_error extra(error::too_many_ranks{{1, 2}}, "1/1", 2, 3);
    assert(std::string(extra.what()) == "FEN rank count is 2; expected 1.");

    const board_fen_error piece(error::invalid_piece{}, "1z6", 1, 2);
    assert(std::string(piece.what()) == "Invalid FEN piece: \"z\".");
    const board_fen_error marker(error::invalid_unmoved_marker{}, "N*7", 0, 2);
    assert(std::string(marker.what()) == "Invalid unmoved marker in FEN: \"N*\". "
        "Only K, R, P, W, k, r, p, and w may be followed by '*'.");

    for(const auto &[start, end] : {std::pair<std::size_t, std::size_t>{2, 1}, {0, 4}, {4, 4}})
    {
        bool rejected = false;
        try { board_fen_error diagnostic(error::invalid_piece{}, "1z1", start, end); }
        catch(const std::invalid_argument &) { rejected = true; }
        assert(rejected);
    }
}

void test_fen_error_ownership()
{
    using error = board_fen_error;
    for(const auto &fen : {std::string("N*"), std::string(128, '1') + "N*"})
    {
        auto copy = [&fen] {
            const board_fen_error original(error::invalid_unmoved_marker{}, fen, fen.size() - 2, fen.size());
            return board_fen_error(original);
        }();
        assert(copy.get_range() == "N*");
        assert(copy.get_range().data() == copy.get_fen().data() + copy.get_start());
        const std::string message = copy.what();
        board_fen_error moved(std::move(copy));
        assert(moved.get_fen() == fen && moved.get_range() == "N*");
        assert(moved.get_range().data() == moved.get_fen().data() + moved.get_start());
        assert(moved.what() == message);

        board_fen_error assigned(error::invalid_piece{}, "z", 0, 1);
        assigned = moved;
        assert(assigned.get_range().data() == assigned.get_fen().data() + assigned.get_start());
        moved = std::move(assigned);
        assert(moved.get_range() == "N*" && moved.what() == message);
        assert(moved.get_range().data() == moved.get_fen().data() + moved.get_start());
    }

    bool caught = false;
    try { throw board_fen_error(error::invalid_piece{}, "z", 0, 1); }
    catch(const std::invalid_argument &error) {
        caught = true;
        assert(std::string(error.what()) == "Invalid FEN piece: \"z\".");
    }
    assert(caught);
}

void test_constructor_diagnostics()
{
    using error = board_fen_error;
    struct example_t {
        std::string fen;
        error::error_type_t reason;
        std::size_t start, end;
    };
    const example_t examples[] = {
        {"4/3/4", error::rank_too_short{{4, 3}}, 2, 3},
        {"4/4/3", error::rank_too_short{{4, 3}}, 4, 5},
        {"4//4", error::rank_too_short{{4, 0}}, 2, 2},
        {"4/4/", error::rank_too_short{{4, 0}}, 4, 4},
        {"3/Z/4", error::rank_too_short{{4, 3}}, 0, 1},
        {"4/5/4", error::rank_too_long{{4, 5}}, 2, 3},
        {"4/K*3Q/4", error::rank_too_long{{4, 5}}, 5, 6},
        {"4/4K*/4", error::rank_too_long{{4, 5}}, 3, 5},
        {"4/4", error::too_few_ranks{{3, 2}}, 3, 3},
        {"", error::too_few_ranks{{3, 0}}, 0, 0},
        {" \t", error::too_few_ranks{{3, 0}}, 2, 2},
        {"4/4/4/4", error::too_many_ranks{{3, 4}}, 5, 6},
        {"4/4/4/", error::too_many_ranks{{3, 4}}, 5, 6},
        {"4/4/4/4/4", error::too_many_ranks{{3, 5}}, 5, 6},
        {"4/1z2/4", error::invalid_piece{}, 3, 4},
        {"4/N*3/4", error::invalid_unmoved_marker{}, 2, 4},
        {"4/#*3/4", error::invalid_unmoved_marker{}, 2, 4},
        {"4/4*/4", error::invalid_unmoved_marker{}, 2, 4},
        {"4/K**3/4", error::invalid_unmoved_marker{}, 4, 5},
    };
    for(const auto &example : examples)
    {
        bool rejected = false;
        try { board position(example.fen, 4, 3); }
        catch(const board_fen_error &error) {
            rejected = true;
            std::visit([&]<typename T>(const T &expected) {
                const auto *actual = std::get_if<T>(&error.get_error_type());
                assert(actual);
                if constexpr(std::is_base_of_v<board_fen_error::invalid_shape, T>) {
                    assert(actual->expected == expected.expected);
                    assert(actual->actual == expected.actual);
                }
            }, example.reason);
            assert(error.get_fen() == example.fen);
            assert(error.get_start() == example.start && error.get_end() == example.end);
            assert(error.get_range() == example.fen.substr(example.start, example.end - example.start));
        }
        assert(rejected);
    }
}

void test_constructor_and_edit_flags()
{
    const std::string fen = "K*R*P*W*k*r*p*w*/QBNUSYCD/qbnusycd/1#2#3";
    board position(fen, 8, 4);
    assert(position.get_fen<true>(8, 4) == fen);
    assert(position.get_fen(8, 4) == "KRPWkrpw/QBNUSYCD/qbnusycd/1#2#3");
    bitboard_t unmoved = 0;
    for(int x = 0; x < BOARD_LENGTH; ++x)
        unmoved |= pmask(ppos(x, 3));
    assert(position.umove() == unmoved);
    for(int y = 4; y < BOARD_LENGTH; ++y)
        for(int x = 0; x < BOARD_LENGTH; ++x)
            assert(position.get_piece(ppos(x,y)) == WALL_PIECE);

    position.set_piece(ppos(1, 3), ROOK_W);
    assert(position.umove() == (unmoved & ~pmask(ppos(1, 3))));
    const auto edited = position.replace_piece(ppos(6, 3), NO_PIECE);
    assert(edited->umove() == (position.umove() & ~pmask(ppos(6, 3))));
    assert(position.get_piece(ppos(6, 3)) == PAWN_B);

    // Preserve whitespace and zero-width digits in otherwise complete input.
    assert(board(" 1 0 # 2 / 4 / #### ", 4, 3).get_fen(4, 3) == "1#2/4/####");
    for(const auto *invalid : {"N*", "#*", "1*", "Z", "9", "4/4/4/4", "4/4/3K*Q"})
    {
        bool rejected = false;
        try { board position(invalid, 4, 3); }
        catch(const board_fen_error &) { rejected = true; }
        assert(rejected);
    }
}

void test_text_output()
{
    const board position("k*1#1/####/R*1K*#", 4, 3);
    const std::string wall_row = "################\n";
    const std::string expected = wall_row + wall_row + wall_row + wall_row + wall_row
        + "k . ##. ########\n"
        + wall_row
        + "R . K ##########\n";
    assert(position.to_string() == expected);
}

void test_walls_and_unmoved_pieces()
{
    const std::string fen = "k1#1/####/R*1K*#";
    const board position(fen, 4, 3);
    assert(position.get_fen<true>(4, 3) == fen);
    assert(position.get_fen(4, 3) == "k1#1/####/R1K#");
    const board restored(position.get_fen<true>(4, 3), 4, 3);
    for(int pos = 0; pos < BOARD_SIZE; ++pos)
        assert(position.get_piece(pos) == restored.get_piece(pos));
    assert(position.umove() == restored.umove());
    assert(position.wall() == restored.wall());

    assert(board("k###/4/K###", 4, 3).get_fen(4, 3) == "k###/4/K###");

    const board empty("4/4/4", 4, 3);
    const auto edited = empty.replace_piece(ppos(1, 1), WALL_PIECE);
    assert(edited->get_fen(4, 3) == "4/1#2/4");
    assert(empty.get_fen(4, 3) == "4/4/4");
}

void test_dimensions()
{
    for(int height = 1; height <= BOARD_LENGTH; ++height)
        for(int width = 1; width <= BOARD_LENGTH; ++width)
        {
            std::string fen;
            for(int row = 0; row < height; ++row)
            {
                if(row) fen += '/';
                fen += std::string(width, '#');
            }
            const board position(fen, width, height);
            assert(position.get_fen(width, height) == fen);
        }

    board clipped("k3/4/3K", 4, 3);
    clipped.set_piece(ppos(7, 7), QUEEN_W);
    assert(clipped.get_fen(4, 3) == "k3/4/3K");
    for(const auto &[width, height] : {std::pair{0, 8}, {9, 8}, {8, 0}, {8, 9}})
    {
        bool rejected = false;
        try { clipped.get_fen(width, height); }
        catch(const std::invalid_argument &) { rejected = true; }
        assert(rejected);
    }
}

void test_game_round_trip()
{
    auto original = game::from_pgn("[Board \"Custom\"] [Size \"4x3\"] [k1#1/####/R*1K*#:0:1:w]");
    assert(std::get<3>(original.get_current_boards().front()) == "k1#1/####/R1K#");
    const auto exported = original.show_pgn();
    assert(exported.find("k1#1/####/R*1K*#") != std::string::npos);
    const auto restored = game::from_pgn(exported);
    assert(original.get_current_boards() == restored.get_current_boards());
    assert(original.get_current_state().get_board(0, 1, false)->umove()
           == restored.get_current_state().get_board(0, 1, false)->umove());
}
} /* anonymous namespace */

int run()
{
    test_fen_error_messages_and_ranges();
    test_fen_error_ownership();
    test_constructor_diagnostics();
    test_constructor_and_edit_flags();
    test_text_output();
    test_walls_and_unmoved_pieces();
    test_dimensions();
    test_game_round_trip();
    return 0;
}
} /* namespace fen_tests */

namespace import_diagnostic_tests
{
void run()
{
    const auto message = [](const std::string &pgn) -> std::string {
        try { game::from_pgn(pgn); }
        catch(const pgn_validation_error &error) { return error.what(); }
        throw std::logic_error("Expected a failed PGN import");
    };
    const std::string standard = "[Board \"Standard - Turn Zero\"]\n";
    assert(message(standard + "1. (0T3)Wb2b2?! {comment}")
        == "Invalid move: (0T3)Wb2b2?!");
    assert(message(standard + "(1. (0T3)Wb2b2) 1. e4")
        == "Invalid move: (0T3)Wb2b2");

    const std::string knights = "[Board \"Custom\"]\n[7k/8/8/8/8/8/3N3N/K7:0:1:w]\n";
    const auto ambiguous = message(knights + "1. Nf3");
    assert(ambiguous == "Ambiguous move: Nf3; possible candidates: (0T1)Nd2f3, (0T1)Nh2f3 (totally 2)"
        || ambiguous == "Ambiguous move: Nf3; possible candidates: (0T1)Nh2f3, (0T1)Nd2f3 (totally 2)");
    // Each displayed candidate resolves the ambiguity and can be imported.
    game::from_pgn(knights + "1. (0T1)Nd2f3");
    game::from_pgn(knights + "1. (0T1)Nh2f3");

    const std::string three_knights = "[Board \"Custom\"]\n[7k/8/8/8/3N4/8/3N3N/K7:0:1:w]\n";
    const auto multiple = message(three_knights + "1. Nf3");
    assert(multiple.starts_with("Ambiguous move: Nf3; possible candidates: "));
    assert(multiple.ends_with(", ... (totally 3)"));

    const std::string promotion = "[Board \"Custom\"]\n[2r4k/1P1P4/8/8/8/8/8/K7:0:1:w]\n";
    const auto underpromotion = message(promotion + "1. xc8=N");
    assert(underpromotion == "Ambiguous move: xc8=N; possible candidates: (0T1)b7xc8=N, (0T1)d7xc8=N (totally 2)"
        || underpromotion == "Ambiguous move: xc8=N; possible candidates: (0T1)d7xc8=N, (0T1)b7xc8=N (totally 2)");
}
} /* namespace import_diagnostic_tests */

namespace illegal_history_tests
{
namespace
{
const std::string standard = "[Board \"Standard - Turn Zero\"]\n";
const std::string mate = "1. f3 / e5 2. g4 / Qh4";
const std::string illegal_setup = R"([Board "Custom"]
[Size "8x8"]
[Timeline "odd"]
[7k/8/8/8/8/8/8/K7:0:1:w]
[7k/3r4/8/8/3K4/8/8/8:1:1:b]
)";

template<class Load>
void rejects(Load load, const std::string &reason)
{
    try { load(); }
    catch(const pgn_validation_error &error) {
        assert(std::string(error.what()).find(reason) != std::string::npos);
        return;
    }
    throw std::logic_error("Expected PGN loading to fail");
}

void test_strict_state_loading()
{
    const state legal = state::from_pgn(standard + "1. e4 / e5");
    assert(legal.show_fen() == state::from_pgn<true>(standard + "1. e4 / e5").show_fen());
    const state checked = state::from_pgn(standard + mate);
    assert(checked.has_phantom_check());
    rejects([] { state::from_pgn(illegal_setup); }, "Illegal initial setup");
    rejects([] { game::from_pgn(illegal_setup); }, "Illegal initial setup");
    const state unchecked = state::from_pgn<true>(illegal_setup);
    assert(unchecked.find_royal_exposure(true));
    const auto setup_ast = pgnparser(illegal_setup).parse_game();
    rejects([&] { state::from_ast(*setup_ast); }, "Illegal initial setup");
    assert(state::from_ast<true>(*setup_ast).show_fen() == unchecked.show_fen());

    // The final position is safe, but an earlier white action was illegal.
    const std::string recovered = standard + mate + " 3. a3 / a6";
    const state final = state::from_pgn<true>(recovered);
    assert(!final.find_royal_exposure(false));
    const auto recovered_ast = pgnparser(recovered).parse_game();
    assert(state::from_ast<true>(*recovered_ast).show_fen() == final.show_fen());
    rejects([&] { state::from_pgn(recovered); }, "Illegal action");
    rejects([&] { game::from_pgn(recovered); }, "Illegal action");
    rejects([&] { state::from_ast(*pgnparser(recovered).parse_game()); }, "Illegal action");
    rejects([] {
        state::from_pgn("[Size \"8x8\"] [7k/8/8/8/8/8/8/K7:0:1:w]"
            " [7k/8/8/8/8/8/8/K7:1:1:w] 1. (0T1)a1b1");
    }, "Cannot submit");
}

void test_unsafe_validation()
{
    // UNSAFE relaxes royal safety, not move resolution or completed turns.
    for(const std::string &pgn : {
        standard + "1. e2e5",
        std::string("[Promotions \"q\"]") + standard,
        std::string("[Timeline \"sideways\"]") + standard,
        std::string("[Size \"8x8\"] [7k/8/8/8/8/8/8/K7:0:1:w]"
            " [7k/8/8/8/8/8/8/K7:1:1:w] 1. (0T1)a1b1")}) {
        const auto ast = pgnparser(pgn).parse_game();
        rejects([&] { state::from_pgn<true>(pgn); }, "");
        rejects([&] { state::from_ast<true>(*ast); }, "");
    }
    const std::string bad_fen = "[Board \"Custom\"] [7k/8/8/8/8/8/8/K6:0:1:w]";
    const auto ast = pgnparser(bad_fen).parse_game();
    for(bool from_text : {false, true}) {
        bool rejected = false;
        try {
            if(from_text) state::from_pgn<true>(bad_fen);
            else state::from_ast<true>(*ast);
        } catch(const board_fen_error &error) {
            rejected = true;
            assert(error.get_pgn_source() && *error.get_pgn_source() == bad_fen);
            assert(error.get_pgn_range());
        }
        assert(rejected);
    }
    bool rejected_syntax = false;
    try { state::from_pgn<true>(standard + "1. {"); }
    catch(const parse_error &) { rejected_syntax = true; }
    assert(rejected_syntax);
}

void test_illegal_roots()
{
    auto g = game::from_pgn(illegal_setup, {.allow_illegal_history = true});
    assert(g.has_illegal_history());
    assert(g.get_match_status() == game::match_status_t::ILLEGAL_HISTORY);
    assert(!g.suggest_action());
    assert(!g.has_parent());
    assert(g.apply_move(ext_move("(0T1)a1b2")));
    assert(g.has_illegal_history());
    assert(g.submit(true));
    assert(g.has_illegal_history());
    assert(!g.suggest_action());
    const auto exported = g.show_pgn(pgn_options::SHOW_ALL);
    auto restored = game::from_pgn(exported, {.allow_illegal_history = true});
    assert(restored.has_illegal_history());
    assert(restored.get_current_boards() == g.get_current_boards());
    rejects([&] { game::from_pgn(exported); }, "Illegal initial setup");

    // An inactive opponent endpoint is covered by the same loading policy.
    const std::string inactive = R"([Board "Custom"]
[7k/8/8/8/8/8/8/K7:0:1:w]
[8/8/8/8/8/8/8/8:1:1:b]
[7k/3r4/8/8/3K4/8/8/8:2:1:b]
)";
    rejects([&] { state::from_pgn(inactive); }, "Illegal initial setup");
    assert(game::from_pgn(inactive, {.allow_illegal_history = true}).has_illegal_history());
}

void test_submission_policy()
{
    for(const std::string &pgn : {standard + mate,
        standard + "1. e4 / f6 2. d4 / g5 3. Qh5"})
    {
        state draft = state::from_pgn(pgn);
        const bool black = draft.get_present().c();
        assert(!draft.test_present());
        assert(!draft.can_submit());
        assert(!draft.submit());
        const full_move move(black ? "(0T3)a7a6" : "(0T3)a2a3");
        assert(draft.apply_move(move));
        assert(draft.test_present());
        const auto boards = draft.get_boards();
        const auto present = draft.get_present();
        assert(!draft.can_submit());
        assert(!draft.submit());
        assert(draft.get_boards() == boards);
        assert(draft.get_present() == present);
        assert(draft.submit<true>());
        assert(draft.find_royal_exposure(false));

        auto g = game::from_pgn(pgn);
        assert(!g.can_submit() && !g.can_submit(true));
        assert(!g.submit() && !g.submit(true));
        assert(g.apply_move(ext_move(move, NO_PIECE)));
        assert(!g.can_submit() && g.can_submit(true));
        const auto cached = g.get_cached_moves();
        const auto history = g.show_pgn();
        assert(g.get_submit_action()); // Completion-only extraction.
        assert(!g.submit());
        assert(g.get_cached_moves() == cached);
        assert(g.show_pgn() == history);
        assert(!g.has_illegal_history());
        assert(g.submit(true));
        assert(g.has_illegal_history());

        const state initial = state::from_pgn(pgn);
        const action act = action::from_vector({ext_move(move, NO_PIECE)}, initial);
        assert(!initial.can_apply(act));
        auto root = gnode<>::create_root(initial, {});
        auto child = gnode<>::create_child(root.get(), std::nullopt, act, {}, true);
        assert(child->get_state().get_boards() == draft.get_boards());
        assert(child->get_state().get_present() == draft.get_present());
        // Flagged history permits replaying a further exposed action.
        auto permissive = game::from_pgn(pgn, {.allow_illegal_history = true});
        assert(permissive.apply_move(ext_move(move, NO_PIECE)));
        assert(permissive.submit(true));
        permissive.visit_parent();
        assert(permissive.visit_child(act));
    }
    state legal = state::from_pgn(standard);
    assert(legal.apply_move(full_move("(0T1)e2e4")));
    assert(legal.test_present() && legal.can_submit());
    assert(legal.submit()); // Giving check is allowed; exposing one's own royal is not.
}

void test_manual_submission_and_navigation()
{
    auto g = game::from_pgn(standard + mate);
    assert(!g.has_illegal_history());
    assert(g.get_match_status() == game::match_status_t::BLACK_WINS);
    assert(g.apply_move(ext_move("(0T3)a2a3")));
    assert(!g.has_illegal_history()); // Drafts do not change submitted history.
    const auto unsafe_action = *g.get_submit_action();
    assert(g.submit(true));
    assert(g.has_illegal_history());
    assert(!g.suggest_action());
    assert(g.get_match_status() == game::match_status_t::ILLEGAL_HISTORY);
    assert(g.apply_move(ext_move("(0T3)a7a6")));
    assert(g.submit(true));
    assert(!g.get_current_state().find_royal_exposure(false));
    assert(g.has_illegal_history());
    assert(!g.suggest_action());
    const action continuation = action::from_vector(
        {ext_move(full_move("(0T4)a3a4"), NO_PIECE)}, g.get_current_state());
    assert(!g.get_current_state().can_apply(continuation));
    assert(g.add_child(continuation)); // Preserve permissive insertion on flagged history.

    g.visit_parent();
    assert(g.has_illegal_history());
    assert(g.get_child_actions().front().is_illegal);
    g.visit_parent();
    assert(!g.has_illegal_history());
    assert(g.get_child_actions().front().act == unsafe_action);
    assert(g.get_child_actions().front().is_illegal);
    assert(g.get_match_status() == game::match_status_t::BLACK_WINS);
    const auto before = g.show_pgn();
    assert(!g.visit_child(action{}));
    assert(g.show_pgn() == before);
    assert(g.visit_child(unsafe_action));
    assert(g.has_illegal_history());
    g.visit_parent();
    assert(g.apply_move(ext_move("(0T3)a2a3")));
    assert(g.submit(true)); // Reusing the existing child preserves its flag.
    assert(g.has_illegal_history());
}

void test_variations_and_export()
{
    const std::string pgn = standard + "1. f3 / e5 (2. g4 / Qh4 3. a3 / a6) 2. g3";
    rejects([&] { game::from_pgn(pgn); }, "Illegal action");
    auto g = game::from_pgn(pgn, {.allow_illegal_history = true});
    assert(!g.has_illegal_history());
    assert(g.get_match_status() == game::match_status_t::PLAYING);
    const auto flags = pgn_options::SHOW_CAPTURE | pgn_options::SHOW_PROMOTION
        | pgn_options::SHOW_MATE | pgn_options::SHOW_OUTCOME | pgn_options::SHOW_SHORT;
    const auto exported = g.show_pgn(flags);
    // The illegal child must not act as a witness against Qh4 being checkmate.
    assert(exported.find("Qh4#") != std::string::npos);
    auto restored = game::from_pgn(exported, {.allow_illegal_history = true});
    assert(!restored.has_illegal_history());
    g.visit_parent(); // After e5, both the legal and illegal variations start legally.
    for(const auto &summary : g.get_child_actions()) {
        assert(!summary.is_illegal);
    }

    auto branch = game::from_pgn(standard + mate + " 3. a3 / a6", {.allow_illegal_history = true});
    const auto path = branch.show_pgn(flags, false);
    assert(path.find("Qh4#") != std::string::npos);
    assert(path.find("a3+") == std::string::npos);
    assert(path.find("a6+") == std::string::npos);
    auto roundtrip = game::from_pgn(path, {.allow_illegal_history = true});
    assert(roundtrip.has_illegal_history());
    assert(roundtrip.get_current_boards() == branch.get_current_boards());

    branch.visit_parent();
    branch.visit_parent();
    const auto unsafe = branch.get_child_actions().front();
    assert(branch.get_child_actions(flags).front().pgn.find('+') == std::string::npos);
    assert(unsafe.is_illegal);
    assert(branch.delete_child(unsafe.act));
    assert(!branch.has_illegal_history());
    assert(branch.get_match_status() == game::match_status_t::BLACK_WINS);
}

void test_clone_and_inheritance()
{
    const state initial = state::from_pgn(standard);
    auto root = gnode<>::create_root(initial, {});
    assert(!root->has_illegal_history());
    auto illegal = gnode<>::create_child(root.get(), initial, action{}, {}, true);
    auto descendant = gnode<>::create_child(illegal.get(), initial, action{}, {});
    assert(descendant->has_illegal_history());
    illegal->add_child(std::move(descendant));
    root->add_child(std::move(illegal));
    const auto clone = root->clone();
    assert(!clone->has_illegal_history());
    assert(clone->get_children().front()->has_illegal_history());
    assert(clone->get_children().front()->get_children().front()->has_illegal_history());
    std::ostringstream status;
    status << game::match_status_t::ILLEGAL_HISTORY;
    assert(status.str() == "ILLEGAL_HISTORY");
}
} /* anonymous namespace */

int run()
{
    try {
        test_strict_state_loading();
        test_unsafe_validation();
        test_illegal_roots();
        test_submission_policy();
        test_manual_submission_and_navigation();
        test_variations_and_export();
        test_clone_and_inheritance();
    }
    catch(const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
} /* namespace illegal_history_tests */

namespace stack_safety_tests
{
namespace {
const std::string setup = "[Board \"Custom\"]\n[7k/8/8/8/8/8/8/K7:0:1:w]\n";

std::string quiet_move(int ply)
{
    switch(ply % 4)
    {
        case 0: return "Ka1b1";
        case 1: return "Kh8g8";
        case 2: return "Kb1a1";
        default: return "Kg8h8";
    }
}

void test_long_import()
{
    std::string input = setup;
    for(int ply = 0; ply < 1024; ++ply)
        input += "/ " + quiet_move(ply) + "\n";
    {
        auto g = game::from_pgn(input);
        assert((g.get_current_present() == std::pair{513, false}));
        for(int ply = 0; ply < 1024; ++ply)
        {
            assert(g.has_parent());
            g.visit_parent();
        }
        assert(!g.has_parent());
        assert(g.delete_child(g.get_child_actions().front().act));
        assert(g.get_child_actions().empty());
    }
    // Validation failure must also clean up the entire partial history.
    bool rejected = false;
    try { game::from_pgn(input + "/ Ka1h7"); }
    catch(const pgn_validation_error &) { rejected = true; }
    assert(rejected);
}

void test_nested_variations()
{
    std::string input = setup;
    constexpr int depth = 256;
    for(int ply = 0; ply < depth; ++ply)
        input += "(/ " + quiet_move(ply) + " ";
    for(int ply = depth - 1; ply >= 0; --ply)
    {
        const std::string from = quiet_move(ply).substr(0, 3);
        input += ") / " + from + (ply % 2 ? "h7" : "a2") + " ";
    }
    auto g = game::from_pgn(input);
    // Import ends at the outermost main continuation, not a nested leaf.
    assert((g.get_current_present() == std::pair{1, true}));
    g.visit_parent();
    for(int ply = 0; ply < depth; ++ply)
    {
        const auto children = g.get_child_actions();
        assert(children.size() == 2);
        assert(g.visit_child(children.front().act));
    }
    assert(g.get_child_actions().empty());

    // Missing closing parentheses retain the established syntax diagnostic.
    input = setup;
    for(int ply = 0; ply < depth; ++ply)
        input += "(/ " + quiet_move(ply) + " ";
    bool rejected = false;
    try { pgnparser(input).parse_game(); }
    catch(const parse_error &) { rejected = true; }
    assert(rejected);
}

void test_deep_destruction()
{
    constexpr int depth = 50000;
    {
        pgnparser_ast::gametree root;
        auto *node = &root;
        for(int i = 0; i < depth; ++i)
        {
            auto child = std::make_unique<pgnparser_ast::gametree>();
            auto *next = child.get();
            std::get<pgnparser_ast::gametree::variations_t>(node->variations_or_outcome)
                .emplace_back(pgnparser_ast::actions{}, std::move(child));
            node = next;
        }
        // Move assignment must safely destroy the previous long tree too.
        root = pgnparser_ast::gametree{};
    }
    struct payload {
        int *live;
        payload *parent = nullptr;
        bool alive = true;
        explicit payload(int &live) : live(&live) { ++live; }
        payload(payload &&other) noexcept : live(other.live) { ++*live; }
        ~payload()
        {
            assert(!parent || parent->alive);
            alive = false;
            --*live;
        }
    };
    int live = 0;
    {
        auto root = gnode<payload>::create_root(state::from_pgn(setup), payload{live});
        auto *node = root.get();
        for(int i = 0; i < depth; ++i)
        {
            auto *child = node->add_child(gnode<payload>::create_child(node, std::nullopt, action{}, payload{live}));
            child->get_info().parent = &node->get_info();
            node = child;
        }
        assert(live == depth + 1);
        root.reset();
    }
    assert(live == 0);
}
} /* anonymous namespace */

void run()
{
    test_long_import();
    test_nested_variations();
    test_deep_destruction();
}
} /* namespace stack_safety_tests */

namespace initial_position_tests
{
void run()
{
    const std::string setup = "[Board \"Standard - Turn Zero\"]\n";
    const auto initial = game::from_pgn(setup);
    const auto endpoint = game::from_pgn(setup + "1. e4 / e5");
    const auto only = game::from_pgn(setup + "1. e4 / e5",
        {.allow_illegal_history = true, .initial_position_only = true});
    assert(only.get_current_boards() == initial.get_current_boards());
    assert(only.get_current_boards() != endpoint.get_current_boards());
    assert(!only.has_parent());
    assert(only.get_child_actions().empty());
    const auto ast = pgnparser(setup + "1. e4 / e5").parse_game(true);
    assert(ast && std::get<pgnparser_ast::gametree::variations_t>(ast->gt.variations_or_outcome).empty());
    // A valid first token can precede malformed history; only full loading
    // parses the rest. Existing one-token lexer lookahead is intentional.
    const auto partial = game::from_pgn(setup + "1. {",
        {.allow_illegal_history = true, .initial_position_only = true});
    assert(partial.get_current_boards() == initial.get_current_boards());
    bool rejected = false;
    try { game::from_pgn(setup + "1. {"); }
    catch(const parse_error &) { rejected = true; }
    assert(rejected);
    const std::string exposed = "[Board \"Custom\"]\n[7k/3r4/8/8/3K4/8/8/8:0:1:b]";
    const auto unchecked = game::from_pgn(exposed,
        {.allow_illegal_history = true, .initial_position_only = true});
    assert(!unchecked.get_current_boards().empty());
    rejected = false;
    try { game::from_pgn(exposed, {.initial_position_only = true}); }
    catch(const pgn_validation_error &) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { game::from_pgn("[Board \"Custom\"] [bad:0:1:w]",
        {.allow_illegal_history = true, .initial_position_only = true}); }
    catch(const board_fen_error &) { rejected = true; }
    assert(rejected);
}
} /* namespace initial_position_tests */

int main()
{
    initial_position_tests::run();
    stack_safety_tests::run();
    import_diagnostic_tests::run();
    std::cerr << "Running FEN tests\n";
    if(const int result = fen_tests::run(); result != 0)
        return result;
    std::cerr << "Running multiverse tests\n";
    if(const int result = multiverse_tests::run(); result != 0)
        return result;
    std::cerr << "Running gametree tests\n";
    if(const int result = gametree_tests::run(); result != 0)
        return result;
    std::cerr << "Running hash tests\n";
    if(const int result = hash_tests::run(); result != 0)
        return result;
    if(const int result = import_deduplication_tests::run(); result != 0) return result;
    if(const int result = deletion_tests::run(); result != 0) return result;
    std::cerr << "Running preview tests\n";
    if(const int result = preview_tests::run(); result != 0)
        return result;
    std::cerr << "Running illegal-history tests\n";
    if(const int result = illegal_history_tests::run(); result != 0)
        return result;
    std::cerr << "Running mate tests\n";
    if(const int result = mate_tests::run(); result != 0)
        return result;
    return 0;
}

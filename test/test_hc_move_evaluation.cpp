#undef NDEBUG
#include <cassert>
#include <cstddef>

#include "move_info_evaluation.h"
#include "pgnparser.h"

namespace
{
struct coverage
{
    std::size_t moves = 0;
    bool check = false;
    bool jump = false;
    bool promotion = false;
};

state parse(const std::string &pgn)
{
    return state(*pgnparser(pgn).parse_game());
}

void compare(const state &position, coverage &seen)
{
    auto [info, space] = HC_info::build_HC(position);
    (void)space;
    semimove_feature evaluator(info);
    for(index_t axis = 0; axis < info.universe.dimension(); ++axis)
    {
        for(index_t coordinate : info.universe[axis])
        {
            const auto cached = info.get_move_boards(axis, coordinate);
            if(!cached)
            {
                assert(!evaluator.is_check(axis, coordinate));
                assert(hc_move_info_score(info, evaluator, axis, coordinate,
                                          default_move_info_weights) == 0.0f);
                continue;
            }
            state after = position;
            const ext_move prepared(cached->move, position);
            assert(after.apply_move<true>(prepared));
            const bool checking = evaluator.is_check(axis, coordinate);
            const state check_view = after.phantom(!position.get_present().second);
            assert(checking == static_cast<bool>(
                check_view.find_checks(position.get_present().second).first()));
            assert(checking == (evaluator.is_physical_check(axis, coordinate)
                                || evaluator.is_sp_check(axis, coordinate)));
            if (evaluator.is_historical_check(axis, coordinate)) assert(checking);
            ++seen.moves;
            seen.check |= checking;
            seen.jump |= static_cast<bool>(
                cached->move.move_type(position) & special_move_t::SUPERPHYSICAL);
            seen.promotion |= static_cast<bool>(
                cached->move.move_type(position) & special_move_t::PROMOTION);
        }
    }
}

void test_superphysical_and_historical_checks()
{
    const char* source = "7k/8/8/8/8/8/8/1R2K3";
    const char* target = "8/8/8/8/8/8/8/k7";
    const auto verify = [](const state& position, bool historical) {
        auto [info, space] = HC_info::build_HC(position);
        (void)space;
        semimove_feature features(info);
        const full_move checking("(0T1)b1a1");
        const full_move physical("(0T1)b1b8");
        bool found = false;
        bool found_physical = false;
        for (index_t axis = 0; axis < info.universe.dimension(); ++axis)
            for (index_t coordinate : info.universe[axis]) {
                const auto boards = info.get_move_boards(axis, coordinate);
                if (!boards) continue;
                if (boards->move == physical) {
                    found_physical = true;
                    assert(features.is_physical_check(axis, coordinate));
                    assert(!features.is_sp_check(axis, coordinate));
                    assert(!features.is_historical_check(axis, coordinate));
                }
                if (boards->move != checking) continue;
                found = true;
                assert(!features.is_physical_check(axis, coordinate));
                assert(features.is_sp_check(axis, coordinate));
                assert(features.is_historical_check(axis, coordinate)
                       == historical);
                assert(features.is_check(axis, coordinate));
            }
        assert(found);
        assert(found_physical);
    };
    multiverse_odd endpoint({{0,1,false,source},{1,1,true,target}});
    verify(state(endpoint), false);
    multiverse_odd history({{0,1,false,source},{1,2,false,target},
                            {1,2,true,target},{1,3,false,target},
                            {1,3,true,target}});
    verify(state(history), true);
}
}

int main()
{
    test_superphysical_and_historical_checks();
    coverage seen;
    compare(parse("[Size \"4x4\"]\n[3k/4/Kp2/1R2:0:1:w]"), seen);

    multiverse_odd promotion_boards({
        {0, 1, false, "7k/P7/8/8/8/8/8/K7"},
    });
    compare(state(promotion_boards, promotion_options::QUEEN), seen);

    multiverse_odd jump_boards({
        {-1, 1, false, "r6k/8/8/8/8/8/8/K7"},
        {0, 1, false, "7k/P7/8/8/8/8/8/K7"},
    });
    compare(state(jump_boards, promotion_options::QUEEN), seen);

    assert(seen.moves > 0);
    assert(seen.check);
    assert(seen.jump);
    assert(seen.promotion);
}

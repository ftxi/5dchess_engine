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

state parse(const std::string& pgn)
{
    return state(*pgnparser(pgn).parse_game());
}

void compare(const state& position, coverage& seen)
{
    auto [info, space] = HC_info::build_HC(position);
    (void)space;
    hc_move_evaluation evaluator(info);
    for(index_t axis = 0; axis < info.universe.dimension(); ++axis)
    {
        for(index_t coordinate : info.universe[axis])
        {
            const auto cached = info.get_move_boards(axis, coordinate);
            const auto actual = evaluator.features(axis, coordinate);
            if(!cached)
            {
                assert(actual
                       == (std::array<float, move_info_weights::COUNT>{}));
                continue;
            }
            const auto old = position.get_move_info(cached->move);
            assert(actual == extract_move_info_features(old));
            ++seen.moves;
            seen.check |= actual[move_info_weights::CHECK] != 0.0f;
            seen.jump |= static_cast<bool>(
                old.special_move & special_move_t::SUPERPHYSICAL);
            seen.promotion |= static_cast<bool>(
                old.special_move & special_move_t::PROMOTION);
        }
    }
}
}

int main()
{
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

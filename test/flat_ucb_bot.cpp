#undef NDEBUG
#include <cassert>
#include <memory>

#include "flat_ucb.h"
#include "io_handler.h"

template<class Engine>
void test_engine()
{
    Engine bot(std::make_unique<stdio_handler>(), 1, 0);
    bot.set_position("startpos", "");

    const std::optional<action> best_move = bot.find_best_move(1, std::nullopt, {});
    assert(best_move.has_value());
    assert(best_move->get_length() > 0);
}

int main()
{
    test_engine<flat_ucb_engine>();
    test_engine<flat_ucb_weighted_engine>();
    return 0;
}

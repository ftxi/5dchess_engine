#undef NDEBUG
#include <cassert>
#include <memory>

#include "flat_ucb.h"
#include "io_handler.h"

int main()
{
    flat_ucb_engine bot(std::make_unique<stdio_handler>(), 1, 0);
    bot.set_position("startpos", "");

    const std::optional<action> best_move = bot.find_best_move(1, std::nullopt, {});
    assert(best_move.has_value());
    assert(best_move->get_length() > 0);
    return 0;
}

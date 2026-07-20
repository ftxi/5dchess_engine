#ifndef MONKEY_H
#define MONKEY_H

#include "uci.h"

/*
* Monkey engine: a simple engine that randomly choose an action if possible.
* It's like a monkey banging on a typewriter.
* This engine is used for testing and debugging purposes.
*/
class monkey_engine : public engine
{
public:
    monkey_engine(std::unique_ptr<io_handler> io_handler)
    : engine(std::move(io_handler)) {}
    void initialize() override {};
    std::optional<action> find_best_move(std::optional<int> depth_limit, std::optional<int> time_limit_ms, std::stop_token stop_token) override;
};

#endif /* MONKEY_H */

#include "state.h"
#include "pgnparser.h"
#include "game.h"

std::string str = R"(
132
)";

game tree_search(std::string pgn, unsigned int depth)
{
    return game::from_pgn(pgn);
}

int main()
{
    return 0;
}
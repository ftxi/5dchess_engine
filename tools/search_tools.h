#ifndef SEARCH_TOOLS_H
#define SEARCH_TOOLS_H

#include <optional>
#include <utility>

#include "state.h"

enum class search_mode
{
    balanced,
    naive,
    stable,
    iterative,
    mixed,
};

std::pair<search_mode, int> parse_search_args(
    int argc, const char *argv[], int start_index = 1);

std::optional<moveseq> find_first_action(state &s, search_mode mode);

generator<moveseq> naive_search(state s);

template<bool PRINT=false>
void count_balanced(state s, int count);
template<bool PRINT=false>
void count_stable(state s, int count);
template<bool PRINT=false>
void count_iterative(state s, int count);
template<bool PRINT=false>
void count_mixed(state s, int count);
template<bool PRINT=false>
void count_naive(state s, int count);

void diff(state s);

#endif /* SEARCH_TOOLS_H */

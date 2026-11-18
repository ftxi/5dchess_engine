#ifndef SEARCH_TOOLS_H
#define SEARCH_TOOLS_H

#include <optional>
#include <cstddef>
#include <utility>

#include "state.h"

enum class search_mode
{
    balanced,
    naive,
    stable,
    deferred,
    iterative,
    fine,
};

std::pair<search_mode, int> parse_search_args(
    int argc, const char *argv[], int start_index = 1);

std::optional<moveseq> find_first_action(state &s, search_mode mode);
std::size_t count_actions(state s, search_mode mode, int limit);

generator<moveseq> naive_search(state s);

enum class hc_policy;
template<hc_policy Policy, bool Print = false>
std::size_t count_hc(state s, int limit);
template<bool PRINT=false>
std::size_t count_naive(state s, int limit);
template<bool PRINT=false>
std::size_t count_fine(state s, int limit);

void diff(state s);

#endif /* SEARCH_TOOLS_H */

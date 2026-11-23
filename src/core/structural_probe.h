#ifndef STRUCTURAL_PROBE_H
#define STRUCTURAL_PROBE_H

#include "geometry.h"
#include <compare>
#include <cstddef>
#include <memory>
#include <vector>

namespace structural_probe
{
struct board_key
{
    int timeline, time;
    auto operator<=>(const board_key &) const = default;
};

enum class kind { physical, null, departure, arrival };

struct coordinate
{
    unsigned value;
    kind type;
    unsigned departure_axis = 0, departure_value = 0;
    board_key from{}, to{};
    int playable_endpoint = -1;
};
using axes = std::vector<std::vector<coordinate>>;

enum class result { sat, unsat, unknown };

class model
{
    struct implementation;
    std::unique_ptr<implementation> impl;
public:
    explicit model(const axes &coordinates, unsigned first_branch, bool enforce_order = true);
    ~model();
    result check(const HC &domain, unsigned timeout_ms);
};
} /* namespace structural_probe */

#endif

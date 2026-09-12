#ifndef MOVE_GEOMETRY_H
#define MOVE_GEOMETRY_H

#include <array>
#include "vec4.h"

inline constexpr std::array orthogonal_tl_directions = {
    vec4(0, 0, 0, 1),
    vec4(0, 0, 0, -1),
    vec4(0, 0, -1, 0)
};

inline constexpr std::array diagonal_tl_directions = {
    vec4(0, 0, 1, 1),
    vec4(0, 0, 1, -1),
    vec4(0, 0, -1, 1),
    vec4(0, 0, -1, -1)
};

inline constexpr std::array both_tl_directions = {
    vec4(0, 0, 0, 1),
    vec4(0, 0, 0, -1),
    vec4(0, 0, -1, 0),
    vec4(0, 0, 1, 1),
    vec4(0, 0, 1, -1),
    vec4(0, 0, -1, 1),
    vec4(0, 0, -1, -1)
};

inline constexpr std::array double_orthogonal_tl_directions = {
    vec4(0, 0, 0, 2),
    vec4(0, 0, 0, -2),
    vec4(0, 0, -2, 0)
};

inline constexpr std::array purely_superphysical_knight_directions = {
    vec4(0, 0, 2, 1),
    vec4(0, 0, 1, 2),
    vec4(0, 0, -2, 1),
    vec4(0, 0, 1, -2),
    vec4(0, 0, 2, -1),
    vec4(0, 0, -1, 2),
    vec4(0, 0, -2, -1),
    vec4(0, 0, -1, -2)
};

#endif

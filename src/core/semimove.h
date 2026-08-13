#ifndef SEMIMOVE_H
#define SEMIMOVE_H

#include <variant>
#include <string>
#include <utility>
#include "action.h"

struct physical_move
{
    full_move m;
};
struct arriving_move
{
    full_move m;
};
struct departing_move
{
    vec4 from;
};
struct null_move {};

/*
 A semimove is the semantic value played on one hypercuboid axis:
 - physical_move: a complete move contained on that axis's board;
 - arriving_move: the arrival of a superphysical move on that axis;
 - departing_move: the departure of a superphysical move from that axis; or
 - null_move: no move on that axis.

 An arriving_move retains the complete `full_move`, including both where the
 piece came from and where it arrives. This is to comply with the hypercuboid algorithm. A departing_move stores only its source coordinate; its corresponding arrival and destination are not part of the departing semimove.

 `lan(s)` uses `s` to obtain the moving piece and variant-specific
 timeline notation. Its output has the following forms:

     physical:  (0T1)g1@Nf3
     arriving:  (0T1)e2@K>(-1T1)e3
     departing: (0T1)e2>@K
     null:      @

 Construct a semimove from any of the four alternatives. Use `is<T>()`,
 `get<T>()`, or `get_if<T>()` for typed inspection, or pass an exhaustive
 visitor to `visit()`:

     semimove sm = arriving_move{move};
     if(const auto *arrival = sm.get_if<arriving_move>()) { use(*arrival); }
     auto result = sm.visit(overloads{physical_fn, arriving_fn,
                                      departing_fn, null_fn});

 `hotspot()` returns the relevant full coordinate: the source square for
 physical/departing moves and the destination square for arriving moves. It
 is intentionally undefined for a null move and throws `std::logic_error`.
 */

class semimove
{
    using data_t = std::variant<physical_move, arriving_move, departing_move, null_move>;
    data_t data;

public:
    semimove(physical_move move) : data(std::move(move)) {}
    semimove(arriving_move move) : data(std::move(move)) {}
    semimove(departing_move move) : data(std::move(move)) {}
    semimove(null_move move) : data(std::move(move)) {}

    template<typename T>
    bool is() const noexcept
    {
        return std::holds_alternative<T>(data);
    }

    template<typename T>
    const T &get() const
    {
        return std::get<T>(data);
    }

    template<typename T>
    const T *get_if() const noexcept
    {
        return std::get_if<T>(&data);
    }

    template<typename Visitor>
    decltype(auto) visit(Visitor &&visitor) const
    {
        return std::visit(std::forward<Visitor>(visitor), data);
    }

    std::string to_string() const;
    std::string lan(const state &s) const;
    vec4 hotspot() const;
};


#endif /* SEMIMOVE_H */

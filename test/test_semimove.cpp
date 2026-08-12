#include <cassert>
#include <stdexcept>
#include <string>

#include "core/pgnparser.h"
#include "core/semimove.h"
#include "core/state.h"
#include "misc/utils.h"

int main()
{
    const state s(*pgnparser(R"(
[Board "Standard"]
)").parse_game());

    const semimove physical = physical_move{full_move("(0T1)g1f3")};
    assert(physical.to_string(s) == "(0T1)g1@Nf3");
    assert(physical.is<physical_move>());
    assert(physical.get_if<physical_move>() != nullptr);
    assert(physical.get<physical_move>().m == full_move("(0T1)g1f3"));
    assert(physical.hotspot() == vec4(6, 0, 1, 0));

    const semimove pawn = physical_move{full_move("(0T1)e2e4")};
    assert(pawn.to_string(s) == "(0T1)e2@Pe4");

    const semimove arriving = arriving_move{full_move("(0T1)e1(-1T1)e3")};
    assert(arriving.to_string(s) == "(0T1)e1@K>(-1T1)e3");
    assert(arriving.hotspot() == vec4(4, 2, 1, -1));

    const semimove departing = departing_move{vec4(4, 0, 1, 0)};
    assert(departing.to_string(s) == "(0T1)e1>@K");
    assert(departing.hotspot() == vec4(4, 0, 1, 0));

    const semimove null = null_move{};
    assert(null.to_string(s) == "@");
    bool threw = false;
    try
    {
        (void)null.hotspot();
    }
    catch(const std::logic_error &)
    {
        threw = true;
    }
    assert(threw);

    const int kind = arriving.visit(overloads{
        [](const physical_move &) { return 0; },
        [](const arriving_move &) { return 1; },
        [](const departing_move &) { return 2; },
        [](const null_move &) { return 3; }
    });
    assert(kind == 1);
}

#include <tuple>
#include <ranges>
#include <cassert>
#include "state.h"
#include "pgnparser.h"

std::string str = R"(
[Board "Standard"]
[Mode "5D"]
1. e3 / Nf6
2. Bb5 / e6
3. c3 / Ne4
4. Qb3 / Qf6
5. (0T5)Qb3>>x(0T1)f7~ (>L1) / (1T1)Kxf7
6. (1T2)Nf3 / (1T2)e6
7. (1T3)Nf3>>(1T2)f5 (>L2) / (1T3)Qh4
8. (1T4)e3 / (0T5)Qf6>>x(0T1)f2~ (>L-1)
)";

int main()
{
    std::unique_ptr<state> s = nullptr;
    {
        s = std::make_unique<state>(*pgnparser(str).parse_game());
    }
    std::cout << static_cast<int>(s->get_mate_type()) << "\n";
    return 0;
}

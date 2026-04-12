#include <tuple>
#include <ranges>
#include <cassert>
#include <chrono>
#include <sys/resource.h>
#include "state.h"
#include "pgnparser.h"
#include "hypercuboid.h"

std::string str = R"(
[Mode "5D"]
[Board "Very Small - Open"]

1. Bb2 / Nxb2
2. Nxb2 / Kc3
3. a4 / d1
4. (0T4)Qa4>>(0T3)b3 / (0T4)Qd1>>x(0T2)b1
5. (-1T3)Kxb1 / (-1T3)K>>(0T2)c3
6. (-2T3)R>>(-1T3)b1 / (2T3)B>x(1T3)b3 (-2T3)d1
)";

namespace
{
    long long peak_rss_bytes()
    {
        struct rusage usage {};
        if(getrusage(RUSAGE_SELF, &usage) != 0)
        {
            return -1;
        }
#ifdef __APPLE__
        return static_cast<long long>(usage.ru_maxrss);
#else
        return static_cast<long long>(usage.ru_maxrss) * 1024LL;
#endif
    }

    template <typename Runner>
    void profile_block(const char* label, Runner&& runner)
    {
        const auto start = std::chrono::steady_clock::now();
        const int sequences = runner();
        const auto stop = std::chrono::steady_clock::now();
        const std::chrono::duration<double> elapsed = stop - start;
        const long long rss_bytes = peak_rss_bytes();

        std::cout << label << ": " << sequences << " sequences, "
                  << elapsed.count() << " s, peak RSS ";
        if(rss_bytes >= 0)
        {
            std::cout << (rss_bytes / (1024.0 * 1024.0)) << " MiB";
        }
        else
        {
            std::cout << "unavailable";
        }
        std::cout << std::endl;
    }
}

int main(int argc, char** argv)
{
    std::unique_ptr<state> s = nullptr;
    {
        s = std::make_unique<state>(*pgnparser(str).parse_game());
    }

    const std::string mode = (argc > 1) ? argv[1] : "both";

    {
        if(mode == "baseline" || mode == "both")
        {
            profile_block("baseline", [&]() {
                std::cout << sizeof(index_t) << std::endl;
                return 0;
            });
        }
    }

    {
        if(mode == "search" || mode == "both")
        {
            profile_block("search", [&]() {
                int m = 0;
                auto [hc_info, ss] = HC_info::build_HC(*s);
                for(const auto& moveseq: hc_info.search(ss))
                {
                    (void)moveseq;
                    m++;
                }
                return m;
            });
        }
    }

    {
        if(mode == "psearch" || mode == "both")
        {
            profile_block("psearch", [&]() {
                int n = 0;
                auto [hc_info, ss] = HC_info::build_HC(*s);
                for(const auto& moveseq: hc_info.psearch(ss))
                {
                    (void)moveseq;
                    std::cout << n << ": ";
                    for(const auto& move : moveseq)
                    {
                        std::cout << move << " ";
                    }
                    std::cout << std::endl;
                    n++;
                }
                return n;
            });
        }
    }
    return 0;
}

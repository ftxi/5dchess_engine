#ifndef TURN_H
#define TURN_H
#include <iostream>
#include <string>
#include <utility>

using turn_t = std::pair<int,bool>;

inline turn_t next_turn(turn_t t)
{
    auto [n,c] = t;
    int v = (n<<1 | static_cast<int>(c)) + 1;
    return std::make_pair(v>>1, v&1);
}

inline turn_t previous_turn(turn_t t)
{
    auto [n,c] = t;
    int v = (n<<1 | static_cast<int>(c)) - 1;
    return std::make_pair(v>>1, v&1);
}

inline std::string show_turn(turn_t t)
{
    auto [n,c] = t;
    return std::to_string(n) + (c ? "b": "w");
}

enum class match_status_t {PLAYING, WHITE_WINS, BLACK_WINS, STALEMATE};


[[maybe_unused]]
static std::ostream& operator<<(std::ostream& os, const match_status_t& status)
{
   switch (status)
   {
       case match_status_t::PLAYING:
           os << "PLAYING";
           break;
       case match_status_t::WHITE_WINS:
           os << "WHITE_WINS";
           break;
       case match_status_t::BLACK_WINS:
           os << "BLACK_WINS";
           break;
       case match_status_t::STALEMATE:
           os << "STALEMATE";
           break;
   }
   return os;
}


#endif /* TURN_H */

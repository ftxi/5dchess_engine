#ifndef STATE_H
#define STATE_H

#include "multiverse.h"
#include "actions.h"
#include <string>

struct state {
    multiverse m;
    /*
     `number_activated` is max(abs(white's activated lines), abs(black's activated lines)).
     `present` is in L,T coordinate (i.e. not u,v corrdinated).
     These numbers can be inherited from copy-construction; thus they are not necessarily equal to `m.get_present()`.
    */
    int present, player;

    state(multiverse mtv);

    int new_line() const;

    bool can_submit() const;

    template<bool UNSAFE = false>
    bool apply_move(full_move fm);
    
    /*
     get_timeline_status() returns `std::make_tuple(mandatory_timelines, optional_timelines, unplayable_timelines)`
     where:
     mandatory_timelines are the timelines that current player must make a move on it
     optional_timelines are the timelines that current player can choose to play or not
     unplayable_timelines are the timelines that current player can't place a move on
     */
    std::tuple<std::vector<int>, std::vector<int>, std::vector<int>> get_timeline_status() const;
};

#endif //STATE_H

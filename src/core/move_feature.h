#ifndef MOVE_FEATURE_H
#define MOVE_FEATURE_H

#include <optional>

#include "check_position.h"
#include "geometry.h"

class HC_info;

// True if a royal piece of defender's color is attacked on this board.
bool has_physical_check(const board& position, bool defender);

// Local king-exposure heuristic on a resulting board, separate from move type.
bool has_latent_king_threat(const board& result, int king_xy, bool player);

// Features of one HC coordinate. Physical and arriving coordinates represent
// complete moves; departing and null coordinates have no move-level features.
class semimove_feature
{
    const HC_info& info;
    std::optional<check_position> checks;

    template<check_position::check_kind Kind>
    bool check(index_t axis, index_t coordinate);

public:
    explicit semimove_feature(const HC_info& info);
    bool is_physical_check(index_t axis, index_t coordinate);
    bool is_sp_check(index_t axis, index_t coordinate);
    bool is_historical_check(index_t axis, index_t coordinate);
    bool is_check(index_t axis, index_t coordinate);

    // Optional ordering heuristic; independent of move_type and check rules.
    bool has_latent_king_threat(index_t axis, index_t coordinate) const;
};

#endif /* MOVE_FEATURE_H */

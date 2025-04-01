#include "utils.h"
#include "bitboard.h"

extern const std::array<bitboard_t, BOARD_SIZE> knight_attack_data;

extern const std::array<bitboard_t, BOARD_SIZE> king_attack_data;
                                                                        
inline bitboard_t knight_attack(int pos)
{
    return knight_attack_data[pos];
}

inline bitboard_t king_attack(int pos)
{
    return king_attack_data[pos];
}

extern const std::array<bitboard_t, BOARD_SIZE*BOARD_LENGTH> rook_copy_mask_data;
extern const std::array<bitboard_t, BOARD_SIZE*BOARD_LENGTH> bishop_copy_mask_data;
extern const std::array<bitboard_t, BOARD_SIZE*BOARD_LENGTH> queen_copy_mask_data;

inline bitboard_t rook_copy_mask(int pos, int n)
{
    return rook_copy_mask_data[n << BOARD_BITS | pos];
}

inline bitboard_t bishop_copy_mask(int pos, int n)
{
    return bishop_copy_mask_data[n << BOARD_BITS | pos];
}

inline bitboard_t queen_copy_mask(int pos, int n)
{
    return queen_copy_mask_data[n << BOARD_BITS | pos];
}

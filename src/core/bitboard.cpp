#include "bitboard.h"
#include <iostream>
#include <iomanip>
#include <sstream>

std::string bb_to_string(bitboard_t bb)
{
    std::ostringstream ss;
    ss << "0x" << std::hex << std::setw(16) << std::setfill('0') << bb << std::endl;
    for (int y = BOARD_LENGTH - 1; y >= 0; y--)
    {
        for (int x = 0; x < BOARD_LENGTH; x++)
        {
            ss << ((bb & (static_cast<bitboard_t>(1)<<ppos(x, y))) ? "1 " : ". ");
        }
        ss << "\n";
    }
    return ss.str();
}

std::vector<int> marked_pos(bitboard_t b)
{
    std::vector<int> result;
    while(b)
    {
        int n = bb_get_pos(b);
        result.push_back(n);
        b &= ~(bitboard_t(1) << n);
    }
    return result;
}

const std::array<bitboard_t, BOARD_SIZE> knight_attack_data = generate_array(std::make_index_sequence<BOARD_SIZE>{}, [](size_t pos) -> bitboard_t
{
    bitboard_t z = bitboard_t(1) << pos, bb = 0;
    bb |= shift_north(shift_northwest(z));
    bb |= shift_west(shift_northwest(z));
    bb |= shift_north(shift_northeast(z));
    bb |= shift_east(shift_northeast(z));
    bb |= shift_south(shift_southwest(z));
    bb |= shift_west(shift_southwest(z));
    bb |= shift_south(shift_southeast(z));
    bb |= shift_east(shift_southeast(z));
    return bb;
});

const std::array<bitboard_t, BOARD_SIZE> king_attack_data = generate_array(std::make_index_sequence<BOARD_SIZE>{}, [](size_t pos) -> bitboard_t
{
    bitboard_t z = bitboard_t(1) << pos, bb = 0;
    bb |= shift_north(z);
    bb |= shift_south(z);
    bb |= shift_west(z);
    bb |= shift_east(z);
    bb |= shift_northwest(z);
    bb |= shift_northeast(z);
    bb |= shift_southwest(z);
    bb |= shift_southeast(z);
    return bb;
});

const std::array<bitboard_t, BOARD_SIZE*BOARD_LENGTH> rook_copy_mask_data = generate_array(std::make_index_sequence<BOARD_SIZE*BOARD_LENGTH>{}, [](size_t key) -> bitboard_t
{
    size_t pos = key & ((1 << BOARD_LENGTH) - 1), n = key >> BOARD_BITS;
    bitboard_t a, b, c, d, bb = 0;
    a = b = c = d = bitboard_t(1) << pos;
    for(int i = 1; i < n; i++)
    {
        a = shift_north(a);
        b = shift_south(b);
        c = shift_west(c);
        d = shift_east(d);
        bb |= a | b | c | d;
    }
    return bb;
});

const std::array<bitboard_t, BOARD_SIZE*BOARD_LENGTH> bishop_copy_mask_data = generate_array(std::make_index_sequence<BOARD_SIZE*BOARD_LENGTH>{}, [](size_t key) -> bitboard_t
{
    size_t pos = key & ((1 << BOARD_LENGTH) - 1), n = key >> BOARD_BITS;
    bitboard_t a, b, c, d, bb = 0;
    a = b = c = d = bitboard_t(1) << pos;
    for(int i = 1; i < n; i++)
    {
        a = shift_northwest(a);
        b = shift_northeast(b);
        c = shift_southwest(c);
        d = shift_southeast(d);
        bb |= a | b | c | d;
    }
    return bb;
});

const std::array<bitboard_t, BOARD_SIZE*BOARD_LENGTH> queen_copy_mask_data = generate_array(std::make_index_sequence<BOARD_SIZE*BOARD_LENGTH>{}, [](size_t key) -> bitboard_t
{
    return rook_copy_mask_data[key] | bishop_copy_mask_data[key];
});


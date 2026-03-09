#include <iostream>
#include <algorithm>
#include <random>
#include <cassert>
#include "utils.h"
#include "bitboard.h"
#include "magic.h"

std::string standard_fen = ""
"r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*";

using namespace std;

using uint64 = unsigned long long;

bitboard_t ratt(int xy, bitboard_t blocker)
{
    bitboard_t result = 0;
    int x = xy%BOARD_LENGTH, y = xy/BOARD_LENGTH;
    for(int nx = x+1; nx<BOARD_LENGTH; nx++)
    {
        bitboard_t pmask = bitboard_t(1) << ppos(nx, y);
        result |= pmask;
        if(pmask & blocker)
            break;
    }
    for(int nx = x-1; nx>=0; nx--)
    {
        bitboard_t pmask = bitboard_t(1) << ppos(nx, y);
        result |= pmask;
        if(pmask & blocker)
            break;
    }
    for(int ny = y+1; ny<BOARD_LENGTH; ny++)
    {
        bitboard_t pmask = bitboard_t(1) << ppos(x, ny);
        result |= pmask;
        if(pmask & blocker)
            break;
    }
    for(int ny = y-1; ny>=0; ny--)
    {
        bitboard_t pmask = bitboard_t(1) << ppos(x, ny);
        result |= pmask;
        if(pmask & blocker)
            break;
    }
    return result;
}

bitboard_t batt(int xy, bitboard_t blocker)
{
    bitboard_t result = 0;
    int x = xy%BOARD_LENGTH, y = xy/BOARD_LENGTH;
    for(int nx = x+1, ny=y+1; nx<BOARD_LENGTH && ny<BOARD_LENGTH; nx++, ny++)
    {
        bitboard_t pmask = bitboard_t(1) << ppos(nx, ny);
        result |= pmask;
        if(pmask & blocker)
            break;
    }
    for(int nx = x+1, ny=y-1; nx<BOARD_LENGTH && ny>=0; nx++, ny--)
    {
        bitboard_t pmask = bitboard_t(1) << ppos(nx, ny);
        result |= pmask;
        if(pmask & blocker)
            break;
    }
    for(int nx = x-1, ny=y+1; nx>=0 && ny<BOARD_LENGTH; nx--, ny++)
    {
        bitboard_t pmask = bitboard_t(1) << ppos(nx, ny);
        result |= pmask;
        if(pmask & blocker)
            break;
    }
    for(int nx = x-1, ny=y-1; nx>=0 && ny>=0; nx--, ny--)
    {
        bitboard_t pmask = bitboard_t(1) << ppos(nx, ny);
        result |= pmask;
        if(pmask & blocker)
            break;
    }
    return result;
}

void ASSERT_EQ(auto lhs, auto rhs)
{
    if(lhs != rhs)
    {
        std::cout << "assertion failed:\n";
        std::cout << "lhs = " << lhs << std::endl;
        std::cout << "rhs = " << rhs << std::endl;
    }
    assert(lhs == rhs);
}

std::string generate_random_fen(std::mt19937& gen)
{
    // All possible chess pieces (excluding empty squares)
    const std::vector<unsigned char> pieces = {
        KING_W, QUEEN_W, BISHOP_W,
        KNIGHT_W, ROOK_W, PAWN_W,
        //standard pieces for white

        UNICORN_W, DRAGON_W, BRAWN_W,
        PRINCESS_W, ROYAL_QUEEN_W, COMMON_KING_W,
        //nonstandard pieces for white
        KING_B, QUEEN_B, BISHOP_B,
        KNIGHT_B, ROOK_B, PAWN_B,
        //standard pieces for black

        UNICORN_B, DRAGON_B, BRAWN_B,
        PRINCESS_B, ROYAL_QUEEN_B, COMMON_KING_B};
    
    // Distributions
    std::uniform_int_distribution<> empty_dist(0, 99);  // For empty squares
    std::uniform_int_distribution<> piece_dist(0, static_cast<int>(pieces.size() - 1));  // For piece selection
    std::bernoulli_distribution empty_prob(0.6);  // 60% chance of empty square
    
    std::string fen;
    
    for (int rank = 0; rank < 8; ++rank) {
        int emptyCount = 0;
        std::string rankStr;
        
        for (int file = 0; file < 8; ++file) {
            // Randomly decide if the square should be empty
            bool isEmpty = empty_prob(gen);
            
            if (isEmpty) {
                emptyCount++;
            } else {
                if (emptyCount > 0) {
                    rankStr += std::to_string(emptyCount);
                    emptyCount = 0;
                }
                // Choose a random piece
                char piece = pieces[piece_dist(gen)];
                
                // Ensure kings are present (basic check)
                if ((rank == 0 && file == 4 && piece != 'K') ||
                    (rank == 7 && file == 4 && piece != 'k')) {
                    piece = (rank == 0) ? 'K' : 'k';
                }
                
                rankStr += piece;
            }
        }
        
        // Add remaining empty squares at the end of the rank
        if (emptyCount > 0) {
            rankStr += std::to_string(emptyCount);
        }
        
        fen += rankStr;
        if (rank < 7) {
            fen += '/';
        }
    }
    
    return fen;
}


void test_attacks()
{
    std::random_device rd;  // Seed source
    std::mt19937_64 gen(rd()); // 64-bit Mersenne Twister PRNG
    std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX); // Uniform distribution
    
    for(int i = 0; i < 100000; i++)
    {
        uint64_t random_blocker = dist(gen); // Generate 64-bit random number
        int random_entry = dist(gen) % 64;
        ASSERT_EQ(rook_attack(random_entry, random_blocker), ratt(random_entry, random_blocker));
        ASSERT_EQ(bishop_attack(random_entry, random_blocker), batt(random_entry, random_blocker));
        //ASSERT_EQ(bishop_attack_prototype(random_entry, random_blocker), batt(random_entry, random_blocker));
    }
    cerr << "test_attacks passed" << endl;
}

void test_marked_pos_reverse()
{
    std::mt19937_64 gen(0xdecafbad);
    std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);

    for(int i = 0; i < 1000; ++i)
    {
        bitboard_t b = dist(gen);
        auto forward = marked_pos<false>(b);
        auto reversed = marked_pos<true>(b);
        std::reverse(forward.begin(), forward.end());
        assert(forward == reversed);
    }

    cerr << "test_marked_pos_reverse passed" << endl;
}

//void test_bb_conversion()
//{
//    std::random_device rd;
//    std::mt19937 gen(rd());
//    string rfen = standard_fen;
//    for(int i = 0; i < 100000; i++)
//    {
//        board b(rfen);
//        ASSERT_EQ(b.get_fen(), bx.to_board()->get_fen());
//        rfen = generate_random_fen(gen);
//    }
//    
//    cerr << "test_bb_conversion passed" << endl;
//}

 int main()
 {
     test_attacks();
     test_marked_pos_reverse();
     //test_bb_conversion();
     cerr << "---= test_bitboards.cpp: all passed =---" << endl;
     return 0;
 }

#ifndef AST_H
#define AST_H

#include <iostream>
#include <variant>
#include <optional>
#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <map>
#include "turn.h"



namespace pgnparser_ast {
typedef enum {
    NIL,
    WHITE_SPACE, COMMENT, METADATA,
    TURN,
    LINE, TIME, RELATIVE_SYM, CAPTURE,
    PIECE, CASTLE_KINGSIDE, CASTLE_QUEENSIDE,
    FILE_CHAR,
    EQUAL, ZERO, POSITIVE_NUMBER,
    POSITIVE, NEGATIVE,
    NON_BRANCH_JUMP, BRANCHING_JUMP,
    SOFTMATE, CHECKMATE, EVALUATION_SYM,
    PRESENT_MOVED,
    LEFT_PAREN, RIGHT_PAREN,
    END
} token_t;

struct relative_board {
   std::optional<int> line_difference;
   std::optional<int> time_difference;
   friend std::ostream& operator<<(std::ostream& os, const relative_board& rb);
};
struct absolute_board {
   token_t sign;
   std::optional<int> line;
   std::optional<int> time;
   friend std::ostream& operator<<(std::ostream& os, const absolute_board& ab);
};
struct physical_move {
   std::optional<absolute_board> board;
   token_t castle;
   std::optional<char> piece_name;
   std::optional<char> from_file;
   std::optional<char> from_rank;
   bool capture;
   char to_file;
   char to_rank;
   std::optional<char> promote_to;
   friend std::ostream& operator<<(std::ostream& os, const physical_move& pm);
};
struct superphysical_move {
   std::optional<absolute_board> from_board;
   std::optional<char> piece_name;
   std::optional<char> from_file;
   std::optional<char> from_rank;
   token_t jump_indicater;
   bool capture;
   std::variant<std::monostate,absolute_board,relative_board> to_board;
   char to_file;
   char to_rank;
   std::optional<char> promote_to;
   friend std::ostream& operator<<(std::ostream& os, const superphysical_move& sm);
};
struct move {
    std::variant<physical_move, superphysical_move> data;
    friend std::ostream& operator<<(std::ostream& os, const move& mv);
};
struct actions {
    std::vector<move> moves;
    std::vector<std::string> comments;
    friend std::ostream& operator<<(std::ostream& os, const actions& ac);
};
struct gametree {
    std::vector<std::pair<actions, std::unique_ptr<gametree>>> variations;
    friend std::ostream& operator<<(std::ostream& os, const gametree& gt);
};
struct game {
    std::map<std::string, std::string> headers;
    std::vector<std::tuple<std::string, token_t, int, int, bool>> boards;
    gametree gt;
    std::vector<std::string> comments;
    friend std::ostream& operator<<(std::ostream& os, const game& g);
};

} //namespace
#endif // AST_H

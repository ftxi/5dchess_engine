#include <locale>
#include "pgnparser.h"
#include "utils.h"
#include "vec4.h"
#include <charconv>
#include <sstream>
#include <limits>
#include <stack>

//#define DEBUGMSG
#include "debug.h"

template <typename T>
bool match_opt(const std::optional<T> &simple, const std::optional<T> &full)
{
    if(simple)
        return simple == full;
    else
        return true;
}

using namespace pgnparser_ast;


void pgnparser::fail(parse_error::error_type_t reason) const
{
    throw parse_error(std::move(reason), source, {buffer.start, buffer.end});
}

void pgnparser::record_expected(std::vector<std::string> expected)
{
    if(!furthest_error || buffer.start > furthest_error->get_start())
        furthest_error.emplace(parse_error::unexpected_input{std::move(expected)},
            source, source_range{buffer.start, buffer.end});
}


std::vector<std::string> views_to_strings(const std::vector<std::string_view> &views)
{
    std::vector<std::string> result(views.size());
    std::transform(views.begin(), views.end(), result.begin(),
                   [](std::string_view v) {
                       return std::string(v);
                   });
    return result;
}

pgnparser::pgnparser(std::string msg, bool ck, turn start_turn)
    : check_turn_number(ck), source(std::make_shared<const std::string>(std::move(msg))), input(*source)
{
    buffer.current = input.begin();
    buffer.before_turn_zero = start_turn == turn{};
    buffer.turn = buffer.before_turn_zero ? turn{} : start_turn.previous_turn();
    next_token();
}

/* ***LEXER*** */

void pgnparser::next_token()
{
    buffer.previous_end = buffer.end;
    buffer.start = buffer.current - input.begin();
    buffer.end = buffer.start;
    
    if(buffer.current == input.end())
    {
        buffer.token = END;
        dprint("token:END");
        return;
    }
    switch(*buffer.current)
    {
        case 'L':
            buffer.token = LINE; dprint("token:LINE"); buffer.current++; break;
        case 'T':
            buffer.token = TIME; dprint("token:TIME"); buffer.current++; break;
        case '$':
            buffer.token = RELATIVE_SYM; dprint("token:RELATIVE_SYM"); buffer.current++; break;
        case 'x':
            buffer.token = CAPTURE; dprint("token:CAPTURE"); buffer.current++; break;
        case 'P':
        case 'W':
        case 'K':
        case 'C':
        case 'Q':
        case 'Y':
        case 'S':
        case 'N':
        case 'R':
        case 'B':
        case 'U':
        case 'D':
            buffer.token = PIECE;
            buffer.piece = *buffer.current;
            dprint("token:PIECE", buffer.piece);
            buffer.current++;
            break;
        case 'O':
            if(input.end()-buffer.current >= 3
               && buffer.current[1] == '-' && buffer.current[2] == 'O')
            {
                buffer.current += 3;
                if(input.end()-buffer.current >= 2
                   && buffer.current[0] == '-' && buffer.current[1] == 'O')
                {
                    buffer.token = CASTLE_QUEENSIDE;
                    dprint("token:CASTLE_QUEENSIDE");
                    buffer.current += 2;
                }
                else
                {
                    buffer.token = CASTLE_KINGSIDE;
                    dprint("token:CASTLE_KINGSIDE");
                }
            }
            else
            {
                buffer.end = std::min(input.size(), buffer.start + 1);
                fail(parse_error::unexpected_input{{"'O-O'", "'O-O-O'"}});
            }
            break;
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
        case 'g':
        case 'h':
            buffer.token = FILE_CHAR;
            buffer.file = *buffer.current;
            dprint("token:FILE_CHAR", buffer.file);
            buffer.current++;
            break;
        case '=':
            buffer.token = EQUAL; dprint("token:EQUAL"); buffer.current++; break;
        case '0':
            buffer.current++;
            if(buffer.current == input.end() || *buffer.current != '-')
            {
                buffer.token = ZERO;
                dprint("token:ZERO");
                break;
            }
            buffer.current++;
            if(buffer.current == input.end() || *buffer.current != '1')
                throw parse_error(parse_error::unexpected_input{{"'1' after '0-'"}}, source,
                    {static_cast<std::size_t>(buffer.current - input.begin()),
                     std::min(input.size(), static_cast<std::size_t>(buffer.current - input.begin()) + 1)});
            buffer.current++;
            buffer.token = BLACK_WINS;
            break;
        case '1':
            if(input.end() - buffer.current >= 2 && buffer.current[1] == '-')
            {
                buffer.current += 2;
                if(buffer.current == input.end() || *buffer.current != '0')
                    throw parse_error(parse_error::unexpected_input{{"'0' after '1-'"}}, source,
                        {static_cast<std::size_t>(buffer.current - input.begin()),
                         std::min(input.size(), static_cast<std::size_t>(buffer.current - input.begin()) + 1)});
                buffer.current++;
                buffer.token = WHITE_WINS;
                break;
            }
            else if(input.substr(buffer.start, 4) == "1/2-")
            {
                const static std::string_view expected = "1/2-1/2";
                if(input.substr(buffer.start, expected.size()) != expected)
                    throw parse_error(parse_error::unexpected_input{{"'1/2-1/2'"}}, source,
                        {buffer.start, std::min(input.size(), buffer.start + expected.size())});
                buffer.current += expected.size();
                buffer.token = DRAW;
                break;
            }
            [[fallthrough]];
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        {
            const std::size_t begin = buffer.current - input.begin();
            while(buffer.current != input.end() && '0' <= *buffer.current && *buffer.current <= '9')
            {
                buffer.current++;
            }
            const std::size_t end = buffer.current - input.begin();
            const auto result = std::from_chars(
                input.data() + begin, input.data() + end, buffer.number);
            if(result.ec == std::errc::result_out_of_range)
                throw parse_error(parse_error::integer_out_of_range{{0, std::numeric_limits<unsigned>::max()}},
                    source, {begin, end});
            if(buffer.current!=input.end()
               && (*buffer.current == 'w' || *buffer.current == 'b')
               && (buffer.current+1 != input.end())
               && (*(buffer.current+1) == '.'))
            {
                if(buffer.number > static_cast<unsigned>(vec4::T_MAX))
                    throw parse_error(parse_error::turn_out_of_range{{0, vec4::T_MAX}}, source, {begin, end});
                bool c = *buffer.current == 'b';
                buffer.current++;
                turn newturn{static_cast<int>(buffer.number), c};
                const turn expected = buffer.before_turn_zero ? turn{} : buffer.turn.next_turn();
                if(check_turn_number && newturn != expected)
                {
                    throw parse_error(parse_error::nonconsecutive_turn{expected, newturn},
                        source, {begin, end + 2});
                }
                dprint("token:TURN", buffer.number, c ? "b" : "w");
                buffer.token = TURN;
                buffer.turn = newturn;
                buffer.before_turn_zero = false;
                buffer.current++;
            }
            else if(buffer.current!=input.end() && *buffer.current == '.')
            {
                 if(buffer.number > static_cast<unsigned>(vec4::T_MAX))
                     throw parse_error(parse_error::turn_out_of_range{{0, vec4::T_MAX}}, source, {begin, end});
                 const bool c = false;
                 turn newturn = {static_cast<int>(buffer.number), c};
                 const turn expected = buffer.before_turn_zero ? turn{} : buffer.turn.next_turn();
                 if(check_turn_number && newturn != expected)
                 {
                    throw parse_error(parse_error::nonconsecutive_turn{expected, newturn},
                        source, {begin, end + 1});
                 }
                 dprint("token:TURN", buffer.number, c ? "b" : "w");
                 buffer.token = TURN;
                 buffer.turn = newturn;
                 buffer.before_turn_zero = false;
                 buffer.current++;
            }
            else
            {
                dprint("token:POSITIVE_NUMBER", buffer.number);
                buffer.token = POSITIVE_NUMBER;
            }
            break;
        }
        case '/':
            buffer.turn = buffer.before_turn_zero ? turn{} : buffer.turn.next_turn();
            buffer.before_turn_zero = false;
            dprint("token:TURN /", buffer.turn.t(), buffer.turn.c()?"b":"w");
            buffer.token = TURN;
            buffer.current++;
            break;
        case '+':
            buffer.token = POSITIVE; dprint("token:POSITIVE"); buffer.current++; break;
        case '-':
            buffer.token = NEGATIVE; dprint("token:NEGATIVE"); buffer.current++; break;
        case '*':
            buffer.token = SOFTMATE; dprint("token:SOFTMATE"); buffer.current++; break;
        case '#':
            buffer.token = CHECKMATE; dprint("token:CHECKMATE"); buffer.current++; break;
        case '~':
            buffer.token = PRESENT_MOVED; dprint("token:PRESENT_MOVED"); buffer.current++; break;
        case '!':
        case '?':
        {
            auto start = buffer.current;
            buffer.token = EVALUATION_SYM;
            buffer.current++;
            while(buffer.current != input.end() && (*buffer.current == '?' || *buffer.current == '!'))
                buffer.current++;
            buffer.comment = std::string_view(start, buffer.current);
            dprint("token:EVALUATION_SYM", buffer.comment);
            break;
        }
        case '>':
            buffer.current++;
            if(buffer.current != input.end() && *buffer.current == '>')
            {
                buffer.token = BRANCHING_JUMP;
                buffer.current++;
                dprint("token:BRANCHING_JUMP");
            }
            else
            {
                buffer.token = NON_BRANCH_JUMP;
                dprint("token:NON_BRANCH_JUMP");
            }
            break;
        case '(':
            buffer.token = LEFT_PAREN; dprint("token:LEFT_PAREN"); buffer.current++; break;
        case ')':
            buffer.token = RIGHT_PAREN; dprint("token:RIGHT_PAREN"); buffer.current++; break;
        case '[':
        {
            auto start = buffer.current;
            while(*buffer.current != ']')
            {
                buffer.current++;
                if(buffer.current==input.end())
                    throw parse_error(parse_error::unterminated_metadata{}, source, {buffer.start, input.size()});
            }
            buffer.comment = std::string_view(start+1, buffer.current);
            buffer.token = METADATA;
            dprint("token:METADATA", buffer.comment);
            buffer.current++;
            break;
        }
        case '{':
        {
            auto start = buffer.current;
            int nest_level = 1;
            while(nest_level > 0)
            {
                buffer.current++;
                if(buffer.current==input.end())
                    throw parse_error(parse_error::unterminated_comment{}, source, {buffer.start, input.size()});
                else if(*buffer.current == '{')
                    nest_level++;
                else if(*buffer.current == '}')
                    nest_level--;
            }
            buffer.comment = std::string_view(start+1, buffer.current);
            buffer.token = COMMENT;
            dprint("token:COMMENT", buffer.comment);
            buffer.current++;
            break;
        }
        default:
            if(isspace(static_cast<unsigned char>(*buffer.current)))
            {
                buffer.token = WHITE_SPACE;
                dprint("token:WHITE_SPACE");
                while(buffer.current != input.end() && isspace(static_cast<unsigned char>(*buffer.current)))
                    buffer.current++;
            }
            else
            {
                buffer.end = buffer.start + 1;
                fail(parse_error::unexpected_character{});
            }
    }
    buffer.end = buffer.current - input.begin();
}


#define PARSE_START auto fallback = buffer;
#define PARSE_FAIL { \
    buffer = fallback;\
    dprint("fallback to", std::string(input.begin(),fallback.current), "{<<-here}", std::string(fallback.current, input.end()), "\n[  ...]", __func__, "failed"); \
    return std::nullopt; \
}

void pgnparser::test_lexer()
{
    while(buffer.token != END)
        next_token();
}

/* ***PARSER*** */

/*
 <relative-to-board> ::= '$(' (['L'] <difference> 'T' <difference> | 'L' <difference> | 'T' <difference>) ')'
 <difference> ::= '=' | '+' <positive-buffer.number> | '-' <positive-buffer.number>
 throws error if detected '$(' but the following is not <relative-to-board> syntax
 */
std::optional<relative_board> pgnparser::parse_relative_board()
{
    PARSE_START;  // cache old buffer.current pointer in case it fails
    dprint("parse_relative_board()");
    std::optional<int> ldiff, tdiff;
    bool has_line = true, expect_line = false;
    if(buffer.token != RELATIVE_SYM) PARSE_FAIL;
    next_token();
    if(buffer.token != LEFT_PAREN) PARSE_FAIL;
    next_token();
    if(buffer.token == LINE)
    {
        expect_line = true;
        next_token(); // skip ['L']
    }
    int sign = 0;
    switch (buffer.token) {
        case POSITIVE: sign = 1; next_token(); break;
        case NEGATIVE: sign = -1; next_token(); break;
        case EQUAL: ldiff = 0; next_token(); break;
        default:
            if(expect_line)
                fail(parse_error::unexpected_input{{"a signed timeline difference", "'='"}});
            else
                has_line = false;
    }
    if(has_line)
    {
        if(sign != 0 && buffer.token == POSITIVE_NUMBER)
        {
            ldiff = sign*buffer.number;
            next_token();
        }
        else if(sign == 0 && buffer.token == POSITIVE_NUMBER)
            fail(parse_error::unexpected_input{{"'T'", "')'"}});
        else if(sign != 0 && buffer.token != POSITIVE_NUMBER)
            fail(parse_error::unexpected_input{{"a positive integer"}});
    }
    if(buffer.token == TIME)
    {
        next_token();
        
        switch (buffer.token) {
            case POSITIVE: sign = 1; break;
            case NEGATIVE: sign = -1; break;
            case EQUAL: tdiff = 0; break;
            default:
                fail(parse_error::unexpected_input{{"a signed turn difference", "'='"}});
        }
        next_token();
        if(buffer.token == POSITIVE_NUMBER)
        {
            if(sign != 0 && buffer.token == POSITIVE_NUMBER)
            {
                tdiff = sign*buffer.number;
                next_token();
            }
            else if(sign == 0 && buffer.token == POSITIVE_NUMBER)
                fail(parse_error::unexpected_input{{"'T'", "')'"}});
            else if(sign != 0 && buffer.token != POSITIVE_NUMBER)
                fail(parse_error::unexpected_input{{"a positive integer"}});
        }
    }
    else if(!has_line)
        fail(parse_error::unexpected_input{{"a timeline or turn difference"}});
    if(buffer.token != RIGHT_PAREN)
        fail(parse_error::unexpected_input{{"')'"}});
    next_token();
    
    return relative_board{ldiff, tdiff};
}

/*
 <absolute-to-board> ::= '(' ['L'] <line> 'T' <time> ')' | '(L' <line> ')' | '(T' <time> ')'
 <line> ::= ['+' | '-'] <natural-number>
 throws error if:
 - parsed '(L' but the next is not a <line>
 - parsed '(L+' or '(L-' but the next is not a number
 - parsed '(' <line> 'T' or '(L' <line> 'T' or '(T' but next is not a positive number
 - parsed everything except for ')', which is missing
 fail instead of throw an error in examples such as '(2. Kf7)' or '(16b.Qxg5 17.'
*/
std::optional<absolute_board> pgnparser::parse_absolute_board()
{
    PARSE_START;  // cache old buffer.current pointer in case it fails
    dprint("parse_absolute_board()");
    token_t sign = NIL;
    std::optional<int> l, t;
    bool has_line = true, expect_line = false;
    if(buffer.token != LEFT_PAREN) PARSE_FAIL;
    next_token();
    if(buffer.token == LINE)
    {
        expect_line = true;
        next_token(); // skip ['L']
    }
    switch (buffer.token) {
        case POSITIVE:
        case NEGATIVE:
            sign = buffer.token; next_token(); break;
        case POSITIVE_NUMBER: break;
        case ZERO: break;
        default:
            if(expect_line)
                fail(parse_error::unexpected_input{{"a timeline number"}});
            else
                has_line = false;
    }
    if(has_line)
    {
        if(buffer.token == POSITIVE_NUMBER)
        {
            l = buffer.number;
            next_token();
        }
        else if (buffer.token == ZERO)
        {
            l = 0;
            next_token();
        }
        else if (sign == NIL)
        {
            PARSE_FAIL;
        }
        else
        {
            fail(parse_error::unexpected_input{{"a nonnegative integer"}});
        }
    }
    if(buffer.token == TIME)
    {
        next_token();
        if(buffer.token == POSITIVE_NUMBER)
        {
            t = buffer.number;
            next_token();
        }
        else if (buffer.token == ZERO)
        {
            t = 0;
            next_token();
        }
        else
            fail(parse_error::unexpected_input{{"a turn number"}});
    }
    else if(!has_line)
        PARSE_FAIL;
    if(buffer.token != RIGHT_PAREN) {
        record_expected({"')'"});
        PARSE_FAIL;
    }
    next_token();
    
    return absolute_board{sign, l, t};
}

std::optional<char> pgnparser::parse_promotion()
{
    const bool has_equal = buffer.token == EQUAL;
    if(has_equal)
    {
        next_token();
    }
    if(buffer.token == PIECE)
    {
        const char result = buffer.piece;
        next_token();
        return result;
    }
    if(has_equal)
    {
        fail(parse_error::unexpected_input{{"a promotion piece"}});
    }
    return std::nullopt;
}

/*
 <physical-move> ::= [<absolute-board>] ([<piece-name>] [<file>] [<rank>] ['x'] <file> <rank> [['='] <promote-to>] | 'O-O' | 'O-O-O') [<check-symbol>] [<evaluation-symbol>]
 <to-board> ::= <absote-board> | <relative-board>
 throw an excption if '=' found in the end but no promotion piece given
*/
std::optional<physical_move> pgnparser::parse_physical_move()
{
    PARSE_START;
    dprint("parse_physical_move()");
    std::optional<absolute_board> board;
    std::optional<char> piece_name;
    std::optional<char> from_file;
    std::optional<char> from_rank;
    bool capture = false;
    char to_file;
    char to_rank;
    std::optional<char> promote_to;
    
    board = parse_absolute_board();
    if(buffer.token == CASTLE_KINGSIDE || buffer.token == CASTLE_QUEENSIDE)
    {
        token_t castle = buffer.token;
        next_token();
        if(buffer.token == POSITIVE || buffer.token == SOFTMATE || buffer.token == CHECKMATE)
            next_token();
        if(buffer.token == EVALUATION_SYM)
            next_token();
        return physical_move{board, castle, std::nullopt, std::nullopt, std::nullopt, false, 0, 0, std::nullopt};
    }
    if(buffer.token == PIECE)
    {
        piece_name = buffer.piece;
        next_token();
    }
    if(buffer.token == FILE_CHAR)
    {
        from_file = buffer.file;
        next_token();
    }
    if(buffer.token == POSITIVE_NUMBER)
    {
        from_rank = buffer.number;
        next_token();
    }
    if(from_file.has_value() && from_rank.has_value() && buffer.token != CAPTURE
       && buffer.token != FILE_CHAR)
    {
        if(buffer.token == NON_BRANCH_JUMP || buffer.token == BRANCHING_JUMP
           || parse_relative_board().has_value() || parse_absolute_board().has_value())
            PARSE_FAIL;
        dprint("short physical move");
        to_file = *from_file; from_file = std::nullopt;
        to_rank = *from_rank; from_rank = std::nullopt;
    }
    else
    {
        dprint("long physical move");
        if(buffer.token == CAPTURE)
        {
            capture = true;
            next_token();
        }
        if(buffer.token != FILE_CHAR) {
            record_expected({"a destination file"});
            PARSE_FAIL;
        }
        to_file = buffer.file;
        next_token();
        if(buffer.token != POSITIVE_NUMBER) {
            record_expected({"a destination rank"});
            PARSE_FAIL;
        }
        to_rank = buffer.number;
        next_token();
    }
    promote_to = parse_promotion();
    if(buffer.token == POSITIVE || buffer.token == SOFTMATE || buffer.token == CHECKMATE)
        next_token();
    if(buffer.token == EVALUATION_SYM)
        next_token();
    return physical_move{board, NIL, piece_name, from_file, from_rank, capture, to_file, to_rank, promote_to};
}

/*
 <timeline-comment> ::= <whitespace> '(' {'>L' <line> | '~T' <time>} ')'
 throws exception if parsed (>L or (~T but the rest is not <timeline-comment> syntax
 */
std::optional<std::monostate> pgnparser::parse_timeline_comment()
{
    PARSE_START;
    dprint("parse_timeline_comment()");
    if(buffer.token == WHITE_SPACE)
        next_token();
    if(buffer.token != LEFT_PAREN) PARSE_FAIL;
    next_token();
    switch(buffer.token)
    {
        case NON_BRANCH_JUMP:
            next_token();
            if(buffer.token != LINE) PARSE_FAIL;
            next_token();
            if(buffer.token == POSITIVE || buffer.token == NEGATIVE)
                next_token();
            if(buffer.token != ZERO && buffer.token != POSITIVE_NUMBER)
                fail(parse_error::unexpected_input{{"a timeline number"}});
            next_token();
            break;
        case PRESENT_MOVED:
            next_token();
            if(buffer.token != TIME) PARSE_FAIL;
            next_token();
            if(buffer.token != ZERO && buffer.token != POSITIVE_NUMBER)
                fail(parse_error::unexpected_input{{"a turn number"}});
            next_token();
            break;
        default:
            PARSE_FAIL;
    }
    if(buffer.token != RIGHT_PAREN)
        fail(parse_error::unexpected_input{{"')'"}});
    next_token();
    return std::monostate();
}

/*
 <superphysical-move> ::= [<absolute-board>] [<piece-name>] [<file>] [<rank>] (<jump-indicator> ['x'] | [<jump-indicator>] ['x'] <to-board>) <file> <rank> [['='] <promote-to>] [<check-symbol>] [<present-moved-symbol>] [<evaluation-symbol>] <timeline-comment>*
 <to-board> ::= <absote-board> | <relative-board>
 throw an excption if there parsed through and found one of <jump-indicator> or <to-board>
 but the rest is not <superphysical-move> syntax
 */
std::optional<superphysical_move> pgnparser::parse_superphysical_move()
{
    PARSE_START;
    dprint("parse_superphysical_move()");
    std::optional<absolute_board> from_board;
    std::optional<char> piece_name;
    std::optional<char> from_file;
    std::optional<char> from_rank;
    token_t jump_indicater;
    bool capture = false;
    std::variant<std::monostate,absolute_board,relative_board> to_board;
    char to_file;
    char to_rank;
    std::optional<char> promote_to;
    
    from_board = parse_absolute_board();
    if(buffer.token == PIECE)
    {
        piece_name = buffer.piece;
        next_token();
    }
    if(buffer.token == FILE_CHAR)
    {
        from_file = buffer.file;
        next_token();
    }
    if(buffer.token == POSITIVE_NUMBER)
    {
        from_rank = buffer.number;
        next_token();
    }
    if(buffer.token == NON_BRANCH_JUMP || buffer.token == BRANCHING_JUMP)
    {
        jump_indicater = buffer.token;
        next_token();
    }
    else
        jump_indicater = NIL;
    if(buffer.token == CAPTURE)
    {
        capture = true;
        next_token();
    }
    if(jump_indicater != NIL && buffer.token == FILE_CHAR)
    {
        to_board = std::monostate();
    }
    else if(auto tmp = parse_absolute_board())
    {
        to_board = *tmp;
    }
    else if(auto tmp = parse_relative_board())
    {
        to_board = *tmp;
    }
    else if(jump_indicater == NIL)
    {
        PARSE_FAIL;
    }
    else
    {
        fail(parse_error::unexpected_input{{"a destination board or square"}});
    }
    // if parsing does not fail before this position, we believe this shouldn't be something
    // other than a superphysical move
    if(buffer.token != FILE_CHAR)
        fail(parse_error::unexpected_input{{"a destination file"}});
    to_file = buffer.file;
    next_token();
    if(buffer.token != POSITIVE_NUMBER)
        fail(parse_error::unexpected_input{{"a destination rank"}});
    to_rank = buffer.number;
    next_token();
    promote_to = parse_promotion();
    if(buffer.token == POSITIVE || buffer.token == SOFTMATE || buffer.token == CHECKMATE)
        next_token();
    if(buffer.token == PRESENT_MOVED)
        next_token();
    if(buffer.token == EVALUATION_SYM)
        next_token();
    while(parse_timeline_comment())
        ;
    return superphysical_move{from_board, piece_name, from_file, from_rank, jump_indicater, capture, to_board, to_file, to_rank, promote_to};
}

std::optional<move> pgnparser::parse_move()
{
    const std::size_t start = buffer.start;
    auto pm = parse_physical_move();
    if(pm)
        return move{*pm, source_range{start, buffer.previous_end}};
    else
    {
        auto sm = parse_superphysical_move();
        if(sm)
            return move{*sm, source_range{start, buffer.previous_end}};
        else
            return std::nullopt;
    }
}

std::optional<move> pgnparser::parse_standalone_move()
{
    auto result = parse_move();
    if(!result || buffer.token != END)
    {
        return std::nullopt;
    }
    return result;
}

/*
 parses <space-comment>*
 <space-comment> ::= <whitespace> | <comment>
 iterate through all consecutive spaces/comments and return all comments found
*/
std::vector<std::string_view> pgnparser::parse_comments()
{
    dprint("parse_comments()");
    std::vector<std::string_view> comments;
    while(buffer.token == WHITE_SPACE || buffer.token == COMMENT)
    {
        if(buffer.token == COMMENT)
            comments.push_back(buffer.comment);
        next_token();
    }
    return comments;
}

/*
 <actions> ::= <turn-serial> <space-comment>* <move> {<space-comment>+ <move>}* <space-comment>*
 */
std::optional<actions> pgnparser::parse_actions()
{
    PARSE_START;
    if(buffer.token != TURN) PARSE_FAIL;
    //turn turn = buffer.turn;
    next_token();
    while(buffer.token == WHITE_SPACE || buffer.token == COMMENT)
    {
        next_token();
    }
    std::vector<move> moves;
    std::vector<std::string_view> comments;
    comments.clear();
    std::optional<move> mv_buffer = parse_move();
    if(!mv_buffer.has_value()) {
        record_expected({"a move"});
        PARSE_FAIL;
    }
    moves.push_back(*mv_buffer);
    if(buffer.token != WHITE_SPACE && buffer.token != COMMENT)
        return actions{moves, views_to_strings(comments),
            source_range{fallback.start, moves.back().source->end}};
    else
        comments = parse_comments();
    while((mv_buffer = parse_move()))
    {
        moves.push_back(*mv_buffer);
        comments.clear();
        if(buffer.token != WHITE_SPACE && buffer.token != COMMENT)
            break;
        else
            comments = parse_comments();
    }
    return actions{moves, views_to_strings(comments),
            source_range{fallback.start, moves.back().source->end}};
}

/*
 <game-tree> ::= {'(' <space-comment>* <actions> <space-comment>* <game-tree>')' <space-comment>*}* [<actions> <space-comment>* <game-tree> <space-comment>*]
 */
std::optional<gametree> pgnparser::parse_gametree()
{
    gametree root;
    auto *current = &root;
    auto entry = buffer;
    bool entering = true;
    enum class tree_context { root, continuation, branch };
    auto context = tree_context::root;
    struct branch_frame {
        gametree *parent;
        decltype(buffer) entry;
        tree_context context;
    };
    std::stack<branch_frame> branches;
    while(true)
    {
        if(entering && (buffer.token == WHITE_WINS || buffer.token == BLACK_WINS || buffer.token == DRAW))
        {
            current->variations_or_outcome = buffer.token;
            next_token();
        }
        else
        {
            auto &variations = std::get<gametree::variations_t>(current->variations_or_outcome);
            if(buffer.token == LEFT_PAREN)
            {
                next_token();
                parse_comments();
                auto act = parse_actions();
                if(!act)
                {
                    buffer = entry;
                    if(context == tree_context::root) return std::nullopt;
                    fail(parse_error::unexpected_input{{context == tree_context::branch
                        ? "a game tree branch" : "a game tree continuation"}});
                }
                auto child = std::make_unique<gametree>();
                auto *next = child.get();
                variations.emplace_back(std::move(*act), std::move(child));
                branches.push({current, entry, context});
                current = next;
                entry = buffer;
                entering = true;
                context = tree_context::branch;
                continue;
            }
            auto act = parse_actions();
            if(act)
            {
                auto child = std::make_unique<gametree>();
                auto *next = child.get();
                variations.emplace_back(std::move(*act), std::move(child));
                current = next;
                entry = buffer;
                entering = true;
                context = tree_context::continuation;
                continue;
            }
        }

        if(branches.empty()) return root;
        if(buffer.token != RIGHT_PAREN)
            fail(parse_error::unexpected_input{{"')'"}});
        const auto frame = branches.top();
        branches.pop();
        current = frame.parent;
        entry = frame.entry;
        context = frame.context;
        buffer.turn = entry.turn;
        buffer.before_turn_zero = entry.before_turn_zero;
        next_token();
        parse_comments();
        entering = false;
    }
}

/*
 <game> ::= {<space-comment>* <header>}* {<space-comment>* <board-fen>}* <space-comment>* <game-tree>
 <header> ::= '[' <header-key> <whitespace>+ '"' <header-value> '"]'
 <header-key> ::= 'Size' | 'Promotion' | 'Date' | 'Round' | 'White' | 'Black' | 'Result' | any sequence of words not containing whitespace or '"'
 <header-value> ::= any sequence of words not containing '"'
 <board-fen> ::= '[' <fen-string> ':' <line> ':' <time> ':' {'w' | 'b'} ']'
 <fen-string> ::= any sequence of words not containing ':' or ']' ;to be parsed separately
 */

std::optional<game> pgnparser::parse_game(bool initial_position_only)
{
    dprint("parse_game()");
    pgnparser_ast::header_map headers;
    std::vector<std::tuple<std::string, token_t, int, int, bool>> boards;
    std::vector<std::string_view> comments;
    std::vector<pgnparser_ast::board_source> board_sources;
    std::map<std::string, pgnparser_ast::header_source, pgnparser_ast::header_key_less> header_sources;
    parse_comments();
    while(buffer.token == METADATA)
    {
        const std::string s(buffer.comment);
        const std::size_t content_start = buffer.start + 1;
        const auto header_error = [&](parse_error::error_type_t reason,
            std::size_t start, std::size_t end) -> void {
            throw parse_error(std::move(reason), source, {content_start + start, content_start + end});
        };
        const int len = static_cast<int>(s.size());
        bool saw_quote = false;
        for(auto c:s)
        {
            if(c=='"')
            {
                saw_quote = true;
                break;
            }
        }
        if(!saw_quote)
        {
            // parse as <board-fen>
            try {
                boards.push_back(parse_board_fen_metadata(s));
                board_sources.push_back({{buffer.start, buffer.end},
                    {content_start, content_start + std::get<0>(boards.back()).size()}});
            }
            catch(const parse_error &error) {
                header_error(error.get_error_type(), error.get_start(), error.get_end());
            }
        }
        else
        {
            // parse as <header>
            std::string key, value;
            int now = 0, prev;
            while(now<len && !isspace(static_cast<unsigned char>(s[now])))
                now++;
            if(now == len)
                header_error(parse_error::unexpected_input{{"whitespace after the header key"}}, now, now);
            key = std::string(s.begin(), s.begin() + now);
            while(now < len && isspace(static_cast<unsigned char>(s[now])))
                now++;
            if(now == len || s[now] != '"')
                header_error(parse_error::unexpected_input{{"an opening quote"}}, now, std::min(now + 1, len));
            now++; prev = now;
            while(now<len && s[now] != '"')
                now++;
            if(now == len)
                header_error(parse_error::unterminated_header_value{}, prev - 1, now);
            value = std::string(s.begin() + prev, s.begin() + now);
            if(now+1 != len)
                header_error(parse_error::unexpected_input{{"end of header"}}, now + 1, len);
            auto [_, success] = headers.insert({key, value});
            if(!success)
            {
                for(auto &c : key)
                    c = std::tolower(c, std::locale::classic());
                header_error(parse_error::duplicate_header{key}, 0, key.size());
            }
            header_sources.emplace(key, pgnparser_ast::header_source{{buffer.start, buffer.end},
                {content_start + prev, content_start + now}});
        }
        next_token();
        append_vectors(comments, parse_comments());
    }
    append_vectors(comments, parse_comments());
    gametree gt;
    if(!initial_position_only)
    {
        auto gt_opt = parse_gametree();
        if(!gt_opt) {
            if(furthest_error) throw *furthest_error;
            fail(parse_error::unexpected_input{{"a game tree"}});
        }
        gt = std::move(*gt_opt);
        parse_comments();
        if(buffer.token != END) {
            if(furthest_error && furthest_error->get_start() >= buffer.start)
                throw *furthest_error;
            fail(parse_error::unexpected_input{{"end of input"}});
        }
    }
    return game{headers, boards, std::move(gt), views_to_strings(comments), source,
        std::move(board_sources), std::move(header_sources)};
}

std::tuple<std::string, pgnparser_ast::token_t, int, int, bool> pgnparser::parse_board_fen_metadata(const std::string &s)
{
    const auto error = [&](parse_error::error_type_t reason,
        std::size_t start, std::size_t end) -> void {
        throw parse_error(std::move(reason), std::make_shared<const std::string>(s), {start, end});
    };
    const auto colon = [&](std::size_t start) {
        const auto position = s.find(':', start);
        if(position == std::string::npos)
            error(parse_error::unexpected_input{{"':'"}}, s.size(), s.size());
        return position;
    };
    const std::size_t fen_end = colon(0);
    const std::size_t line_start = fen_end + 1;
    const std::size_t line_end = colon(line_start);
    const std::size_t time_start = line_end + 1;
    const std::size_t time_end = colon(time_start);
    token_t sign = NIL;
    std::size_t number_start = line_start;
    if(number_start < line_end && (s[number_start] == '+' || s[number_start] == '-')) {
        sign = s[number_start] == '-' ? NEGATIVE : POSITIVE;
        number_start++;
    }
    const auto number = [&](std::size_t start, std::size_t end, bool timeline) {
        std::int64_t value = 0;
        const auto begin = start;
        // Preserve acceptance of explicitly positive metadata coordinates.
        if(start < end && s[start] == '+') start++;
        const auto result = std::from_chars(s.data() + start, s.data() + end, value);
        if(result.ec == std::errc::invalid_argument)
            error(parse_error::unexpected_input{{timeline ? "a timeline number" : "a turn number"}}, begin, end);
        const auto maximum = timeline ? vec4::L_MAX : vec4::T_MAX;
        if(result.ec == std::errc::result_out_of_range || value < 0 || value > maximum) {
            if(timeline)
                error(parse_error::timeline_out_of_range{{0, maximum}}, begin, end);
            else
                error(parse_error::turn_out_of_range{{0, maximum}}, begin, end);
        }
        if(result.ptr != s.data() + end)
            error(parse_error::unexpected_input{{"':'"}}, result.ptr - s.data(), end);
        return static_cast<int>(value);
    };
    const int l = number(number_start, line_end, true);
    const int t = number(time_start, time_end, false);
    const std::size_t color = time_end + 1;
    if(color == s.size())
        error(parse_error::unexpected_input{{"'w'", "'b'"}}, color, color);
    bool c = false;
    switch(s[color]) {
        case 'w': case 'W': break;
        case 'b': case 'B': c = true; break;
        default:
            error(parse_error::unexpected_input{{"'w'", "'b'"}}, color, color + 1);
    }
    if(color + 1 != s.size())
        error(parse_error::unexpected_input{{"end of board metadata"}}, color + 1, s.size());
    return std::make_tuple(s.substr(0, fen_end), sign, l, t, c);
}

/* ***MATCHER*** */

bool pgnparser::match_absolute_board(absolute_board simple, absolute_board full)
{
    dprint("match_absolute_board");
    return (!simple.line || std::pair{simple.sign, simple.line} == std::pair{full.sign, full.line})
        && match_opt(simple.time, full.time);
}

bool pgnparser::match_relative_board(relative_board simple, relative_board full)
{
    dprint("match_relative_board");
    return match_opt(simple.line_difference, full.line_difference)
        && match_opt(simple.time_difference, full.time_difference);
}

bool pgnparser::match_physical_move(physical_move a, physical_move b)
{
    dprint("match_physical_move");
#ifdef DEBUGMSG
    std::cerr << "board=" << (a.board ? match_absolute_board(*a.board, *b.board) : true) << ' ';
#endif
    if(!(a.board ? match_absolute_board(*a.board, *b.board) : true))
        return false;
    if(a.castle==CASTLE_KINGSIDE)
        return b.castle == CASTLE_KINGSIDE
        || (b.piece_name == 'K' && b.from_file == 'e' && b.from_rank == 1 && b.to_file == 'g' && b.to_rank == 1)
        || (b.piece_name == 'K' && b.from_file == 'e' && b.from_rank == 8 && b.to_file == 'g' && b.to_rank == 8);
    else if(a.castle==CASTLE_QUEENSIDE)
        return b.castle == CASTLE_QUEENSIDE
        || (b.piece_name == 'K' && b.from_file == 'e' && b.from_rank == 1 && b.to_file == 'c' && b.to_rank == 1)
        || (b.piece_name == 'K' && b.from_file == 'e' && b.from_rank == 8 && b.to_file == 'c' && b.to_rank == 8);
#ifdef DEBUGMSG
    std::cerr
        << "board="        << (a.board ? match_absolute_board(*a.board, *b.board) : true) << ' '
        << "piece_name="   << match_opt(a.piece_name, b.piece_name) << ' '
        << "from_file="    << match_opt(a.from_file, b.from_file) << ' '
        << "from_rank="    << match_opt(a.from_rank, b.from_rank) << ' '
        << "capture="      << (!a.capture || b.capture) << ' '
        << "to_file="      << (a.to_file == b.to_file) << ' '
        << "to_rank="      << (a.to_rank == b.to_rank) << ' '
        << "promote_to="   << match_opt(a.promote_to, b.promote_to) << '\n';
#endif
    return match_opt(a.piece_name, b.piece_name)
        && match_opt(a.from_file, b.from_file)
        && match_opt(a.from_rank, b.from_rank)
        && (!a.capture || b.capture)
        && a.to_file == b.to_file
        && a.to_rank == b.to_rank
        && match_opt(a.promote_to, b.promote_to);
}

bool pgnparser::match_superphysical_move(superphysical_move a, superphysical_move b)
{
    dprint("match_superphysical_move");
    bool tb_match;
    if(std::holds_alternative<std::monostate>(a.to_board))
        tb_match = true;
    else if(a.to_board.index() != b.to_board.index())
        tb_match = false;
    else if(std::holds_alternative<absolute_board>(a.to_board))
        tb_match = match_absolute_board(std::get<absolute_board>(a.to_board), std::get<absolute_board>(b.to_board));
    else// if(std::holds_alternative<relative_board>(a.to_board))
        tb_match = match_relative_board(std::get<relative_board>(a.to_board), std::get<relative_board>(b.to_board));
#ifdef DEBUGMSG
    std::cerr
        << "from_board="     << (a.from_board ? match_absolute_board(*a.from_board, *b.from_board) : true) << ' '
        << "piece_name="     << match_opt(a.piece_name, b.piece_name) << ' '
        << "from_file="      << match_opt(a.from_file, b.from_file) << ' '
        << "from_rank="      << match_opt(a.from_rank, b.from_rank) << ' '
        << "jump_indicater=" << (a.jump_indicater == NIL || a.jump_indicater == b.jump_indicater) << ' '
        << "tb_match="       << tb_match << ' '
        << "capture="        << (!a.capture || b.capture) << ' '
        << "to_file="        << (a.to_file == b.to_file) << ' '
        << "to_rank="        << (a.to_rank == b.to_rank) << ' '
        << "promote_to="     << match_opt(a.promote_to, b.promote_to) << '\n';
#endif
    return (a.from_board ? match_absolute_board(*a.from_board, *b.from_board) : true)
        && match_opt(a.piece_name, b.piece_name)
        && match_opt(a.from_file, b.from_file)
        && match_opt(a.from_rank, b.from_rank)
        && (a.jump_indicater == NIL || a.jump_indicater == b.jump_indicater)
        && tb_match
        && (!a.capture || b.capture)
        && a.to_file == b.to_file
        && a.to_rank == b.to_rank
        && match_opt(a.promote_to, b.promote_to);
}

bool pgnparser::match_move(move a, move b)
{
    dprint("match_move");
    if(a.data.index() != b.data.index())
        return false;
    else if(std::holds_alternative<physical_move>(a.data))
        return pgnparser::match_physical_move(std::get<physical_move>(a.data), std::get<physical_move>(b.data));
    else
        return pgnparser::match_superphysical_move(std::get<superphysical_move>(a.data), std::get<superphysical_move>(b.data));
}

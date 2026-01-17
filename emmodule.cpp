#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <memory>
#include "game.h"
#include "pgnparser.h"

using namespace emscripten;

// Safe wrapper that returns JavaScript object with success/error
emscripten::val create_game_from_pgn(const std::string& pgn) 
{
    emscripten::val result = emscripten::val::object();
    
    try {
        auto game_ptr = std::make_shared<game>(game::from_pgn(pgn));
        result.set("success", true);
        result.set("game", emscripten::val(game_ptr));
        return result;
    }
    catch(const parse_error &e) {
        result.set("success", false);
        result.set("error", "ParseError");
        result.set("message", e.what());
        result.set("type", "parse_error");
        return result;
    }
    catch(const std::invalid_argument &e) {
        result.set("success", false);
        result.set("error", "InvalidArgumentError");
        result.set("message", e.what());
        result.set("type", "invalid_argument");
        return result;
    }
    catch(const std::runtime_error &e) {
        result.set("success", false);
        result.set("error", "RuntimeError");
        result.set("message", e.what());
        result.set("type", "runtime_error");
        return result;
    }
    catch(const std::exception &e) {
        result.set("success", false);
        result.set("error", "Exception");
        result.set("message", e.what());
        result.set("type", "generic_exception");
        return result;
    }
    catch(...) {
        result.set("success", false);
        result.set("error", "UnknownError");
        result.set("message", "Unknown exception occurred");
        result.set("type", "unknown");
        return result;
    }
}

EMSCRIPTEN_BINDINGS(engine) {
    // Enum: piece_t
    enum_<piece_t>("Piece")
        .value("NO_PIECE", NO_PIECE)
        .value("WALL_PIECE", WALL_PIECE)
        .value("KING_UW", KING_UW)
        .value("ROOK_UW", ROOK_UW)
        .value("PAWN_UW", PAWN_UW)
        .value("KING_UB", KING_UB)
        .value("ROOK_UB", ROOK_UB)
        .value("PAWN_UB", PAWN_UB)
        .value("KING_W", KING_W)
        .value("QUEEN_W", QUEEN_W)
        .value("BISHOP_W", BISHOP_W)
        .value("KNIGHT_W", KNIGHT_W)
        .value("ROOK_W", ROOK_W)
        .value("PAWN_W", PAWN_W)
        .value("UNICORN_W", UNICORN_W)
        .value("DRAGON_W", DRAGON_W)
        .value("BRAWN_W", BRAWN_W)
        .value("PRINCESS_W", PRINCESS_W)
        .value("ROYAL_QUEEN_W", ROYAL_QUEEN_W)
        .value("COMMON_KING_W", COMMON_KING_W)
        .value("KING_B", KING_B)
        .value("QUEEN_B", QUEEN_B)
        .value("BISHOP_B", BISHOP_B)
        .value("KNIGHT_B", KNIGHT_B)
        .value("ROOK_B", ROOK_B)
        .value("PAWN_B", PAWN_B)
        .value("UNICORN_B", UNICORN_B)
        .value("DRAGON_B", DRAGON_B)
        .value("BRAWN_B", BRAWN_B)
        .value("PRINCESS_B", PRINCESS_B)
        .value("ROYAL_QUEEN_B", ROYAL_QUEEN_B)
        .value("COMMON_KING_B", COMMON_KING_B)
        ;

    // Enum: match_status_t
    enum_<match_status_t>("match_status_t")
        .value("PLAYING", match_status_t::PLAYING)
        .value("WHITE_WINS", match_status_t::WHITE_WINS)
        .value("BLACK_WINS", match_status_t::BLACK_WINS)
        .value("STALEMATE", match_status_t::STALEMATE)
        ;

    // vec4 class with operator wrappers
    // class_<vec4>("vec4")
    //     .constructor<int, int, int, int>()
    //     .function("l", &vec4::l)
    //     .function("t", &vec4::t)
    //     .function("y", &vec4::y)
    //     .function("x", &vec4::x)
    //     .function("equals", &vec4::operator==)
    //     .function("add", &vec4::operator+)
    //     .function("subtract", &vec4::operator-)
    //     .function("negate", &vec4::operator-)
    //     .function("dot", &vec4::operator*)
    //     // For JavaScript toString()
    //     .function("toString", &vec4::to_string);
    class_<vec4>("vec4")
        .constructor<int, int, int, int>()
        .function("l", &vec4::l)
        .function("t", &vec4::t)
        .function("y", &vec4::y)
        .function("x", &vec4::x)
        .function("toString", &vec4::to_string)
        ;

    // ext_move class
    class_<ext_move>("ext_move")
        .constructor<vec4, vec4, piece_t>()
        .function("get_from", &ext_move::get_from)
        .function("get_to", &ext_move::get_to)
        .function("get_promote", &ext_move::get_promote)
        .function("to_string", &ext_move::to_string)
        .function("equals", &ext_move::operator==)
        .function("toString", select_overload<std::string() const>(&ext_move::to_string))
        ;

    // Class: action
    class_<action>("action")
        // Don't define any constructors → JavaScript cannot construct instances
        // (they can only receive instances returned from C++)
        .function("get_moves", &action::get_moves)
        // Comparison operator
        .function("equals", &action::operator==)
        // String representation
        .function("toString", optional_override([](const action &a) {
            return "<action with " + std::to_string(a.get_moves().size()) + " moves>";
        }))
        ;

    // Factory function for creating games
    function("from_pgn", &create_game_from_pgn);

    // Class: game
    class_<game>("game")
        .smart_ptr<std::shared_ptr<game>>("game")
        .property("metadata", &game::metadata)
        .function("get_current_state", &game::get_current_state)
        .function("get_current_present", &game::get_current_present)
        .function("get_current_boards", &game::get_current_boards)
        .function("get_current_timeline_status", &game::get_current_timeline_status)
        .function("gen_move_if_playable", &game::gen_move_if_playable)
        .function("get_match_status", &game::get_match_status)
        .function("get_movable_pieces", &game::get_movable_pieces)
        .function("is_playable", &game::is_playable)
        .function("can_undo", &game::can_undo)
        .function("can_redo", &game::can_redo)
        .function("can_submit", &game::can_submit)
        .function("undo", &game::undo)
        .function("redo", &game::redo)
        .function("apply_move", &game::apply_move)
        .function("submit", &game::submit)
        .function("currently_check", &game::currently_check)
        .function("get_current_checks", &game::get_current_checks)
        .function("get_board_size", &game::get_board_size)
        .function("suggest_action", &game::suggest_action)
        .function("get_comments", &game::get_comments)
        .function("has_parent", &game::has_parent)
        .function("visit_parent", &game::visit_parent)
        .function("get_child_moves", &game::get_child_moves)
        .function("visit_child", optional_override([](game &g, const action &a) {
            return g.visit_child(a);
        }))
        .function("show_pgn", &game::show_pgn)
        ;

    // Register std::vector types for automatic conversion
    register_vector<ext_move>("VectorExtMove");
    register_vector<std::string>("StringVector");
}

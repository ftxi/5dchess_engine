#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <string>
#include <string_view>
#include <memory>
#include "game.h"
#include "pgnparser.h"

using emscripten::class_;
using emscripten::constant;
using emscripten::function;
using emscripten::optional_override;
using emscripten::val;

namespace
{
// PGN error locations are UTF-8 bytes; JavaScript editor offsets are UTF-16.
std::size_t utf16_offset(std::string_view text, std::size_t end)
{
    std::size_t result = 0;
    for(std::size_t i = 0; i < end; ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        if((byte & 0xc0) == 0x80) continue;
        result += byte >= 0xf0 ? 2 : 1;
    }
    return result;
}

val diagnostic_range(const std::string &input, const std::string &source,
                     std::size_t start, std::size_t end)
{
    if(source != input || start > end || end > input.size()) return val::null();
    val range = val::object();
    range.set("start", utf16_offset(input, start));
    range.set("end", utf16_offset(input, end));
    return range;
}

void set_diagnostic(val &result, const char *category, const char *code,
                    const char *message, val range = val::null(), bool allow = false)
{
    val diagnostic = val::object();
    diagnostic.set("category", category);
    diagnostic.set("code", code);
    diagnostic.set("message", message);
    diagnostic.set("range", range);
    diagnostic.set("canAllowIllegalHistory", allow);
    result.set("diagnostic", diagnostic);
}

const char *error_code(const parse_error::error_type_t &reason)
{
    return std::visit(overloads {
        [](const parse_error::unexpected_character &) { return "unexpected_character"; },
        [](const parse_error::unexpected_input &) { return "unexpected_input"; },
        [](const parse_error::unterminated_comment &) { return "unterminated_comment"; },
        [](const parse_error::unterminated_metadata &) { return "unterminated_metadata"; },
        [](const parse_error::unterminated_header_value &) { return "unterminated_header_value"; },
        [](const parse_error::integer_out_of_range &) { return "integer_out_of_range"; },
        [](const parse_error::turn_out_of_range &) { return "turn_out_of_range"; },
        [](const parse_error::timeline_out_of_range &) { return "timeline_out_of_range"; },
        [](const parse_error::nonconsecutive_turn &) { return "nonconsecutive_turn"; },
        [](const parse_error::duplicate_header &) { return "duplicate_header"; }
    }, reason);
}

const char *error_code(const pgn_validation_error::error_type_t &reason)
{
    return std::visit(overloads {
        [](const pgn_validation_error::invalid_move &) { return "invalid_move"; },
        [](const pgn_validation_error::ambiguous_move &) { return "ambiguous_move"; },
        [](const pgn_validation_error::illegal_promotion &) { return "illegal_promotion"; },
        [](const pgn_validation_error::illegal_move &) { return "illegal_move"; },
        [](const pgn_validation_error::present_not_advanced &) { return "present_not_advanced"; },
        [](const pgn_validation_error::royal_exposed &) { return "royal_exposed"; },
        [](const pgn_validation_error::invalid_size &) { return "invalid_size"; },
        [](const pgn_validation_error::unknown_variant &) { return "unknown_variant"; },
        [](const pgn_validation_error::invalid_timeline &) { return "invalid_timeline"; },
        [](const pgn_validation_error::invalid_promotions &) { return "invalid_promotions"; },
        [](const pgn_validation_error::missing_setup &) { return "missing_setup"; }
    }, reason);
}

const char *error_code(const board_fen_error::error_type_t &reason)
{
    return std::visit(overloads {
        [](const board_fen_error::rank_too_short &) { return "rank_too_short"; },
        [](const board_fen_error::rank_too_long &) { return "rank_too_long"; },
        [](const board_fen_error::too_few_ranks &) { return "too_few_ranks"; },
        [](const board_fen_error::too_many_ranks &) { return "too_many_ranks"; },
        [](const board_fen_error::invalid_piece &) { return "invalid_piece"; },
        [](const board_fen_error::invalid_unmoved_marker &) { return "invalid_unmoved_marker"; }
    }, reason);
}

game::load_options read_load_options(const val &js_options)
{
    game::load_options options;
    if(js_options.isUndefined()) return options;
    if(js_options.isNull() || js_options.typeOf().as<std::string>() != "object" || js_options.isArray())
        val::global("TypeError").new_(std::string("from_pgn options must be an object")).throw_();
    const auto read_boolean = [&js_options](const char *name, bool &target) {
        const val value = js_options[name];
        if(value.isUndefined()) return;
        if(value.typeOf().as<std::string>() != "boolean")
            val::global("TypeError").new_(std::string("from_pgn option ") + name + " must be a boolean").throw_();
        target = value.as<bool>();
    };
    read_boolean("allowIllegalHistory", options.allow_illegal_history);
    read_boolean("initialPositionOnly", options.initial_position_only);
    return options;
}

} /* anonymous namespace */

// Safe wrapper that returns JavaScript object with success/error
val create_game_from_pgn(const std::string &pgn, game::load_options options)
{
    val result = val::object();
    try {
        auto game_ptr = std::make_shared<game>(game::from_pgn(pgn, options));
        result.set("success", true);
        result.set("game", val(game_ptr));
        return result;
    }
    catch(const parse_error &e) {
        result.set("success", false);
        result.set("error", "ParseError");
        result.set("message", e.what());
        result.set("type", "parse_error");
        set_diagnostic(result, "syntax", error_code(e.get_error_type()), e.what(),
            diagnostic_range(pgn, e.get_source(), e.get_start(), e.get_end()));
        return result;
    }
    catch(const pgn_validation_error &e) {
        result.set("success", false);
        result.set("error", "PgnValidationError");
        result.set("message", e.what());
        result.set("type", "pgn_validation_error");
        const auto *location = e.get_location();
        set_diagnostic(result, "validation", error_code(e.get_error_type()), e.what(),
            location ? diagnostic_range(pgn, *location->source, location->range.start, location->range.end)
                     : val::null(),
            std::holds_alternative<pgn_validation_error::royal_exposed>(e.get_error_type()));
        return result;
    }
    catch(const board_fen_error &e) {
        result.set("success", false);
        result.set("error", "InvalidArgumentError");
        result.set("message", e.what());
        result.set("type", "invalid_argument");
        const auto &range = e.get_pgn_range();
        set_diagnostic(result, "validation", error_code(e.get_error_type()), e.what(),
            range ? diagnostic_range(pgn, *e.get_pgn_source(), range->start, range->end) : val::null());
        return result;
    }
    catch(const std::invalid_argument &e) {
        result.set("success", false);
        result.set("error", "InvalidArgumentError");
        result.set("message", e.what());
        result.set("type", "invalid_argument");
        set_diagnostic(result, "runtime", "invalid_argument", e.what());
        return result;
    }
    catch(const std::runtime_error &e) {
        result.set("success", false);
        result.set("error", "RuntimeError");
        result.set("message", e.what());
        result.set("type", "runtime_error");
        set_diagnostic(result, "runtime", "runtime_error", e.what());
        return result;
    }
    catch(const std::exception &e) {
        result.set("success", false);
        result.set("error", "Exception");
        result.set("message", e.what());
        result.set("type", "generic_exception");
        set_diagnostic(result, "runtime", "exception", e.what());
        return result;
    }
    catch(...) {
        result.set("success", false);
        result.set("error", "UnknownError");
        result.set("message", "Unknown exception occurred");
        result.set("type", "unknown");
        set_diagnostic(result, "runtime", "unknown", "Unknown exception occurred");
        return result;
    }
}

///////////////////////////////
// cpp objects ~> js objects //
///////////////////////////////

inline val convert_vector_int_to_js(const std::vector<int> &v)
{
    val arr = val::array();
    for (size_t i = 0; i < v.size(); ++i)
    {
        arr.set(i, v[i]);
    }
    return arr;
}

inline val convert_vec4_to_js(const vec4 &v)
{
    val js_vec4 = val::object();
    js_vec4.set("l", v.l());
    js_vec4.set("t", v.t());
    js_vec4.set("y", v.y());
    js_vec4.set("x", v.x());
    return js_vec4;
}

inline val convert_vector_vec4_to_js(const std::vector<vec4> &vec)
{
    val js_array = val::array();
    for (size_t i = 0; i < vec.size(); ++i)
    {
        js_array.set(i, convert_vec4_to_js(vec[i]));
    }
    return js_array;
}

inline val convert_action_to_js(const action &act)
{
    val js_act = val::array();
    const auto mvs = act.get_moves_view();
    for (size_t i = 0; i < mvs.size(); ++i)
    {
        val js_mv = val::object();
        js_mv.set("from", convert_vec4_to_js(mvs[i].get_from()));
        js_mv.set("to", convert_vec4_to_js(mvs[i].get_to()));
        js_mv.set("promote", static_cast<int>(mvs[i].get_promote()));
        js_act.set(i, js_mv);
    }
    return js_act;
}

inline val convert_metadata_to_js(const std::map<std::string, std::string> &metadata)
{
    val js_metadata = val::object();
    for (const auto &[key, value] : metadata)
    {
        js_metadata.set(key, value);
    }
    return js_metadata;
}

///////////////////////////////
// js objects ~> cpp objects //
///////////////////////////////

inline vec4 convert_js_to_vec4(const val &js_vec4)
{
    int l = js_vec4["l"].as<int>();
    int t = js_vec4["t"].as<int>();
    int y = js_vec4["y"].as<int>();
    int x = js_vec4["x"].as<int>();
    return vec4(x, y, t, l);
}

inline action convert_js_to_action(const val &js_action, const state &s)
{
    std::vector<ext_move> mvs;
    unsigned length = js_action["length"].as<unsigned>();
    for (unsigned i = 0; i < length; ++i)
    {
        val js_mv = js_action[i];
        vec4 from = convert_js_to_vec4(js_mv["from"]);
        vec4 to = convert_js_to_vec4(js_mv["to"]);
        piece_t promote_to = js_mv.hasOwnProperty("promote") ? static_cast<piece_t>(js_mv["promote"].as<int>()) : NO_PIECE;
        mvs.emplace_back(from, to, promote_to);
    }
    return action::from_vector(mvs, s);
}

inline std::map<std::string, std::string> convert_js_to_metadata(const val &js_metadata)
{
    std::map<std::string, std::string> metadata;
    if (js_metadata.isNull() || js_metadata.isUndefined())
    {
        return metadata;
    }

    val object = val::global("Object");
    val keys = object.call<val>("keys", js_metadata);
    unsigned length = keys["length"].as<unsigned>();
    for (unsigned i = 0; i < length; ++i)
    {
        std::string key = keys[i].as<std::string>();
        metadata[key] = js_metadata[key].as<std::string>();
    }
    return metadata;
}

val child_actions_to_js(const game &g, pgn_options show_flags)
{
    val result = val::array();
    auto child_actions = g.get_child_actions(show_flags);
    for(size_t i = 0; i < child_actions.size(); ++i)
    {
        const auto &[act, pgn, is_illegal] = child_actions[i];
        val move_info = val::object();
        move_info.set("action", convert_action_to_js(act));
        move_info.set("pgn", pgn);
        move_info.set("is_illegal", is_illegal);
        result.set(i, move_info);
    }
    return result;
}

/////////////////////////
// emscripten bindings //
/////////////////////////

EMSCRIPTEN_BINDINGS(engine) {
    // Factory function for creating games
    function("from_pgn", optional_override([](const std::string &pgn) {
        return create_game_from_pgn(pgn, {});
    }));
    function("from_pgn", optional_override([](const std::string &pgn, const val &options) {
        return create_game_from_pgn(pgn, read_load_options(options));
    }));

    // Class: game
    class_<game>("game")
        .smart_ptr<std::shared_ptr<game>>("game")
        .property("metadata", &game::metadata)
        .function("has_illegal_history", &game::has_illegal_history)
        .function("get_metadata", optional_override([](const game &self) {
            return convert_metadata_to_js(self.metadata);
        }))
        .function("set_metadata", optional_override([](game &self, val js_metadata) {
            self.metadata = convert_js_to_metadata(js_metadata);
        }))
        .function("get_current_present", optional_override([](const game &self) {
            const auto [t, c] = self.get_current_present();
            val obj = val::object();
            obj.set("t", t);
            obj.set("c", c);
            return obj;
        }))
        .function("get_promotion_options", optional_override([](const game &self) {
            return static_cast<unsigned>(self.get_promotion_options());
        }))
        .function("get_cached_moves", optional_override([](const game &self) {
            val result = val::array();
            auto cached_moves = self.get_cached_moves();
            for (size_t i = 0; i < cached_moves.size(); ++i) 
            {
                const ext_move &m = cached_moves[i];
                val move_info = val::object();
                move_info.set("from", convert_vec4_to_js(m.get_from()));
                move_info.set("to", convert_vec4_to_js(m.get_to()));
                move_info.set("promote_to", static_cast<int>(m.get_promote()));
                result.set(i, move_info);
            }
            return result;
        }))
        .function("get_current_boards", optional_override([](const game &self) {
            val result = val::array();
            auto boards = self.get_current_boards();
            for (size_t i = 0; i < boards.size(); ++i) 
            {
                const auto &[l, t, c, fen] = boards[i];
                val board_info = val::object();
                board_info.set("l", l);
                board_info.set("t", t);
                board_info.set("c", c);
                board_info.set("fen", fen);
                result.set(i, board_info);
            }
            return result;
        }))
        .function("get_phantom_boards_and_checks", optional_override([](const game &self) {
            val result = val::object();
            auto [boards, checks] = self.get_phantom_boards_and_checks();
            
            val boards_array = val::array();
            for (size_t i = 0; i < boards.size(); ++i) 
            {
                const auto &[l, t, c, fen] = boards[i];
                val board_info = val::object();
                board_info.set("l", l);
                board_info.set("t", t);
                board_info.set("c", c);
                board_info.set("fen", fen);
                boards_array.set(i, board_info);
            }
            
            val checks_array = val::array();
            for (size_t i = 0; i < checks.size(); ++i) 
            {
                const auto &fm = checks[i];
                val check_info = val::object();
                check_info.set("from", convert_vec4_to_js(fm.from));
                check_info.set("to", convert_vec4_to_js(fm.to));
                checks_array.set(i, check_info);
            }
            
            result.set("boards", boards_array);
            result.set("checks", checks_array);
            return result;
        }))
        .function("get_current_timeline_status", optional_override([](const game &self) {
            const auto &[mandatory, optional, unplayable] =
                self.get_current_timeline_status();
            val obj = val::object();
            obj.set("mandatory_timelines", convert_vector_int_to_js(mandatory));
            obj.set("optional_timelines", convert_vector_int_to_js(optional));
            obj.set("unplayable_timelines", convert_vector_int_to_js(unplayable));
            return obj;
        }))
        .function("gen_move_if_playable", optional_override([](const game &self, val obj) {
            auto vec = self.gen_move_if_playable(convert_js_to_vec4(obj));
            return convert_vector_vec4_to_js(vec);
        }))
        .function("get_match_status", optional_override([](const game &self) {
            game::match_status_t status = self.get_match_status();
            switch(status)
            {
                case game::match_status_t::PLAYING:
                    if(self.get_unmoved_state().get_present().c())
                        return std::string("Black's Move");
                    else
                        return std::string("White's Move");
                case game::match_status_t::WHITE_WINS:
                    return std::string("White Wins");
                case game::match_status_t::BLACK_WINS:
                    return std::string("Black Wins");
                case game::match_status_t::STALEMATE:
                    return std::string("Stalemate");
                case game::match_status_t::ILLEGAL_HISTORY:
                    return self.get_unmoved_state().get_present().c()
                        ? std::string("Black's Move (Illegal History)")
                        : std::string("White's Move (Illegal History)");
                default:
                    return std::string("Unknown Status");
            }
        }))
        .function("get_movable_pieces", optional_override([](const game &self) {
            auto vec = self.get_movable_pieces();
            return convert_vector_vec4_to_js(vec);
        }))
        .function("is_playable", optional_override([](const game &self, val obj) {
            return self.is_playable(convert_js_to_vec4(obj));
        }))
        .function("can_undo", &game::can_undo)
        .function("can_redo", &game::can_redo)
        .function("can_submit", optional_override([](const game &g, val allow) {
            return g.can_submit(!allow.isUndefined() && !allow.isNull() && allow.as<bool>());
        }))
        .function("undo", &game::undo)
        .function("redo", &game::redo)
        .function("apply_move", optional_override([](game &g, val obj) {
            piece_t pt = obj.hasOwnProperty("promote_to") ? static_cast<piece_t>(obj["promote_to"].as<int>()) : NO_PIECE;
            ext_move m(
                convert_js_to_vec4(obj["from"]),
                convert_js_to_vec4(obj["to"]),
                pt
            );
            return g.apply_move(m);
        }))
        .function("submit", optional_override([](game &g, val allow) {
            return g.submit(!allow.isUndefined() && !allow.isNull() && allow.as<bool>());
        }))
        .function("preview_move", optional_override([](game &g, val obj) {
            piece_t pt = obj.hasOwnProperty("promote_to") ? static_cast<piece_t>(obj["promote_to"].as<int>()) : NO_PIECE;
            return g.preview_move(ext_move(
                convert_js_to_vec4(obj["from"]),
                convert_js_to_vec4(obj["to"]), pt));
        }))
        .function("get_submit_action", optional_override([](const game &g) {
            auto act = g.get_submit_action();
            return act ? convert_action_to_js(*act) : val::null();
        }))
        .function("delete_child", optional_override([](game &g, val js_action) {
            return g.delete_child(convert_js_to_action(js_action, g.get_unmoved_state()));
        }))
        .function("visit_child", optional_override([](game &g, val js_action) {
            return g.visit_child(convert_js_to_action(js_action, g.get_unmoved_state()));
        }))
        .function("discard_preview", &game::discard_preview)
        .function("currently_check", &game::currently_check)
        .function("get_current_checks", optional_override([](const game &self) {
            val result = val::array();
            auto checks = self.get_current_checks();
            for (size_t i = 0; i < checks.size(); ++i) 
            {
                const auto &[from, to] = checks[i];
                val check_info = val::object();
                check_info.set("from", convert_vec4_to_js(from));
                check_info.set("to", convert_vec4_to_js(to));
                result.set(i, check_info);
            }
            return result;
        }))
        .function("get_board_size", optional_override([](const game &self) {
            const auto &[x, y] = self.get_board_size();
            val obj = val::object();
            obj.set("x", x);
            obj.set("y", y);
            return obj;
        }))
        .function("suggest_action", optional_override([](game &g) {
            auto act = g.suggest_action();
            return act ? convert_action_to_js(*act) : val::null();
        }))
        .function("has_child", optional_override([](const game &g, val js_action) {
            return g.has_child(convert_js_to_action(js_action, g.get_unmoved_state()));
        }))
        .function("add_child", optional_override([](game &g, val js_action) {
            return g.add_child(convert_js_to_action(js_action, g.get_unmoved_state()));
        }))
        .function("get_comments", optional_override([](const game &self) {
            val result = val::array();
            auto comments = self.get_comments();
            for (size_t i = 0; i < comments.size(); ++i) 
            {
                result.set(i, comments[i]);
            }
            return result;
        }))
        .function("set_comments", optional_override([](game &self, val js_comments) {
            std::vector<std::string> comments;
            unsigned length = js_comments["length"].as<unsigned>();
            for (unsigned i = 0; i < length; ++i)
            {
                comments.push_back(js_comments[i].as<std::string>());
            }
            self.set_comments(comments);
        }))
        .function("has_parent", &game::has_parent)
        .function("visit_parent", &game::visit_parent)
        .function("get_child_actions", optional_override([](const game &self) {
            return child_actions_to_js(self, pgn_options::SHOW_CAPTURE | pgn_options::SHOW_PROMOTION);
        }))
        .function("get_child_actions", optional_override([](const game &self, uint16_t show_flags) {
            return child_actions_to_js(self, static_cast<pgn_options>(show_flags));
        }))
        .function("get_historical_actions", optional_override([](const game &self) {
            val result = val::array();
            auto hist = self.get_historical_actions();
            for (size_t i = 0; i < hist.size(); ++i) 
            {
                const auto &[act, pgn] = hist[i];
                val move_info = val::object();
                move_info.set("action", convert_action_to_js(act));
                move_info.set("pgn", pgn);
                result.set(i, move_info);
            }
            return result;
        }))
        .function("show_pgn", optional_override([](game &g, uint16_t options, bool complete_game_tree, bool standard_metadata_only) {
            return g.show_pgn(static_cast<pgn_options>(options), complete_game_tree, standard_metadata_only);
        }));
    constant("SHOW_NOTHING", static_cast<uint16_t>(pgn_options::SHOW_NOTHING));
    constant("SHOW_RELATIVE", static_cast<uint16_t>(pgn_options::SHOW_RELATIVE));
    constant("SHOW_PAWN", static_cast<uint16_t>(pgn_options::SHOW_PAWN));
    constant("SHOW_CAPTURE", static_cast<uint16_t>(pgn_options::SHOW_CAPTURE));
    constant("SHOW_PROMOTION", static_cast<uint16_t>(pgn_options::SHOW_PROMOTION));
    constant("SHOW_MATE", static_cast<uint16_t>(pgn_options::SHOW_MATE));
    constant("SHOW_LCOMMENT", static_cast<uint16_t>(pgn_options::SHOW_LCOMMENT));
    constant("SHOW_ALL", static_cast<uint16_t>(pgn_options::SHOW_ALL));
    constant("SHOW_SHORT", static_cast<uint16_t>(pgn_options::SHOW_SHORT));
    constant("SHOW_OUTCOME", static_cast<uint16_t>(pgn_options::SHOW_OUTCOME));
    
    // Export version information
    function("get_version", optional_override([]() {
#ifdef PROJECT_VERSION_STRING
        return std::string(PROJECT_VERSION_STRING);
#else
        return std::string("unknown");
#endif
    }));
}

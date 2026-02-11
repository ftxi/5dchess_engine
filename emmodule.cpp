#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <string>
#include <memory>
#include "game.h"
#include "pgnparser.h"

using namespace emscripten;

// Safe wrapper that returns JavaScript object with success/error
val create_game_from_pgn(const std::string& pgn) 
{
    val result = val::object();
    try {
        auto game_ptr = std::make_shared<game>(game::from_pgn(pgn));
        result.set("success", true);
        result.set("game", val(game_ptr));
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

///////////////////////////////
// cpp objects ~> js objects //
///////////////////////////////

inline val convert_vector_int_to_js(const std::vector<int>& v)
{
    val arr = val::array();
    for (size_t i = 0; i < v.size(); ++i)
    {
        arr.set(i, v[i]);
    }
    return arr;
}

inline val convert_vec4_to_js(const vec4& v)
{
    val js_vec4 = val::object();
    js_vec4.set("l", v.l());
    js_vec4.set("t", v.t());
    js_vec4.set("y", v.y());
    js_vec4.set("x", v.x());
    return js_vec4;
}

inline val convert_vector_vec4_to_js(const std::vector<vec4>& vec)
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
    const std::vector<ext_move> mvs = act.get_moves();
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

///////////////////////////////
// js objects ~> cpp objects //
///////////////////////////////

inline vec4 convert_js_to_vec4(const val& js_vec4)
{
    int l = js_vec4["l"].as<int>();
    int t = js_vec4["t"].as<int>();
    int y = js_vec4["y"].as<int>();
    int x = js_vec4["x"].as<int>();
    return vec4(x, y, t, l);
}

inline action convert_js_to_action(const val& js_action, const state& s)
{
    std::vector<ext_move> mvs;
    unsigned length = js_action["length"].as<unsigned>();
    for (unsigned i = 0; i < length; ++i)
    {
        val js_mv = js_action[i];
        vec4 from = convert_js_to_vec4(js_mv["from"]);
        vec4 to = convert_js_to_vec4(js_mv["to"]);
        piece_t promote_to = js_mv.hasOwnProperty("promote") ? static_cast<piece_t>(js_mv["promote"].as<int>()) : QUEEN_W;
        mvs.emplace_back(from, to, promote_to);
    }
    return action::from_vector(mvs, s);
}

/////////////////////////
// emscripten bindings //
/////////////////////////

EMSCRIPTEN_BINDINGS(engine) {
    // Factory function for creating games
    function("from_pgn", &create_game_from_pgn);

    // Class: game
    class_<game>("game")
        .smart_ptr<std::shared_ptr<game>>("game")
        .property("metadata", &game::metadata)
        .function("get_current_present", optional_override([](const game& self) {
            const auto [t, c] = self.get_current_present();
            val obj = val::object();
            obj.set("t", t);
            obj.set("c", c);
            return obj;
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
        .function("get_current_timeline_status", optional_override([](const game& self) {
            const auto& [mandatory, optional, unplayable] =
                self.get_current_timeline_status();
            val obj = val::object();
            obj.set("mandatory_timelines", convert_vector_int_to_js(mandatory));
            obj.set("optional_timelines", convert_vector_int_to_js(optional));
            obj.set("unplayable_timelines", convert_vector_int_to_js(unplayable));
            return obj;
        }))
        .function("gen_move_if_playable", optional_override([](const game& self, val obj) {
            auto vec = self.gen_move_if_playable(convert_js_to_vec4(obj));
            return convert_vector_vec4_to_js(vec);
        }))
        .function("get_match_status", optional_override([](const game& self) {
            match_status_t status = self.get_match_status();
            switch(status)
            {
                case match_status_t::PLAYING:
                    if(self.get_current_present().second)
                        return std::string("Black's Move");
                    else
                        return std::string("White's Move");
                case match_status_t::WHITE_WINS:
                    return std::string("White Wins");
                case match_status_t::BLACK_WINS:
                    return std::string("Black Wins");
                case match_status_t::STALEMATE:
                    return std::string("Stalemate");
                default:
                    return std::string("Unknown Status");
            }
        }))
        .function("get_movable_pieces", optional_override([](const game& self) {
            auto vec = self.get_movable_pieces();
            return convert_vector_vec4_to_js(vec);
        }))
        .function("is_playable", optional_override([](const game& self, val obj) {
            return self.is_playable(convert_js_to_vec4(obj));
        }))
        .function("can_undo", &game::can_undo)
        .function("can_redo", &game::can_redo)
        .function("can_submit", &game::can_submit)
        .function("undo", &game::undo)
        .function("redo", &game::redo)
        .function("apply_move", optional_override([](game &g, val obj) {
            piece_t pt = obj.hasOwnProperty("promote_to") ? static_cast<piece_t>(obj["promote_to"].as<int>()) : QUEEN_W;
            ext_move m(
                convert_js_to_vec4(obj["from"]),
                convert_js_to_vec4(obj["to"]),
                pt
            );
            return g.apply_move(m);
        }))
        .function("submit", &game::submit)
        .function("currently_check", &game::currently_check)
        .function("get_current_checks", optional_override([](const game &self) {
            val result = val::array();
            auto checks = self.get_current_checks();
            for (size_t i = 0; i < checks.size(); ++i) 
            {
                const auto& [from, to] = checks[i];
                val check_info = val::object();
                check_info.set("from", convert_vec4_to_js(from));
                check_info.set("to", convert_vec4_to_js(to));
                result.set(i, check_info);
            }
            return result;
        }))
        .function("get_board_size", optional_override([](const game &self) {
            const auto& [x, y] = self.get_board_size();
            val obj = val::object();
            obj.set("x", x);
            obj.set("y", y);
            return obj;
        }))
        .function("suggest_action", &game::suggest_action)
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
            val result = val::array();
            auto child_actions = self.get_child_actions();
            for (size_t i = 0; i < child_actions.size(); ++i) 
            {
                const auto& [act, pgn] = child_actions[i];
                val move_info = val::object();
                move_info.set("action", convert_action_to_js(act));
                move_info.set("pgn", pgn);
                result.set(i, move_info);
            }
            return result;
        }))
        .function("get_historical_actions", optional_override([](const game &self) {
            val result = val::array();
            auto hist = self.get_historical_actions();
            for (size_t i = 0; i < hist.size(); ++i) 
            {
                const auto& [act, pgn] = hist[i];
                val move_info = val::object();
                move_info.set("action", convert_action_to_js(act));
                move_info.set("pgn", pgn);
                result.set(i, move_info);
            }
            return result;
        }))
        .function("visit_child", optional_override([](game &g, val js_action) {
            action act = convert_js_to_action(js_action, g.get_unmoved_state());
            return g.visit_child(act);
        }))
        .function("show_pgn", &game::show_pgn);
    
    // Export version information
    function("get_version", optional_override([]() {
#ifdef PROJECT_VERSION_STRING
        return std::string(PROJECT_VERSION_STRING);
#else
        return std::string("unknown");
#endif
    }));
}

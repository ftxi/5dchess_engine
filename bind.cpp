#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>
#include <pybind11/operators.h>
#include <pybind11/functional.h>
#include <ostream>
#include "game.h"
#include "bot/bot.h"

namespace py = pybind11;

PYBIND11_MODULE(engine, m) {
    m.doc() = "5d chess engine"; // optional module docstring
    py::enum_<piece_t>(m, "Piece")
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
        .export_values();  // Exports the values for easy access
    py::enum_<match_status_t>(m, "match_status_t")
        .value("PLAYING", match_status_t::PLAYING)
        .value("WHITE_WINS", match_status_t::WHITE_WINS)
        .value("BLACK_WINS", match_status_t::BLACK_WINS)
        .value("STALEMATE", match_status_t::STALEMATE)
        .def("__str__", [](match_status_t status) {
            std::ostringstream oss;
            oss << status;
            return oss.str();
        });
    /*
    py::class_<board>(m, "board")
        .def(py::init<std::string, int, int>(), 
             py::arg("fen"), 
             py::arg("x_size") = BOARD_LENGTH, 
             py::arg("y_size") = BOARD_LENGTH) // Constructor with parameters
        .def("get_piece", &board::get_piece)
        .def("set_piece", &board::set_piece)
        .def("__str__", &board::to_string);
    */
   /*
    py::class_<multiverse>(m, "multiverse")
        .def(py::init<const std::string&>(), py::arg("input")) // Constructor
        .def("get_board", &multiverse::get_board, py::arg("l"), py::arg("t"), py::arg("c"),
             py::return_value_policy::reference) // Return shared_ptr to board
        .def("__str__", &multiverse::to_string) // String representation of the board
        .def("get_boards", &multiverse::get_boards)
        .def("get_piece", &multiverse::get_piece)
        .def("gen_piece_move", &multiverse::gen_piece_move)
        .def_readwrite("metadata", &multiverse::metadata); // Expose `metadata` map directly
    py::class_<multiverse, std::shared_ptr<multiverse>>(m, "multiverse")
        // Bind the constructor
        .def(py::init<const std::string &>(), py::arg("input"))
        // Bind public methods
        .def("get_board", &multiverse::get_board, py::arg("l"), py::arg("t"), py::arg("c"))
        .def("get_boards", &multiverse::get_boards)
        .def("to_string", &multiverse::to_string)
        .def("inbound", &multiverse::inbound, py::arg("a"), py::arg("color"))
        .def("get_piece", &multiverse::get_piece, py::arg("a"), py::arg("color"))
        .def("gen_piece_move", &multiverse::gen_piece_move, py::arg("p"), py::arg("board_color"));
        // // Bind public member variables
        // .def_readwrite("metadata", &multiverse::metadata);
    py::class_<vec4>(m, "vec4")
        .def(py::init<int, int, int, int>(), py::arg("x"), py::arg("y"), py::arg("t"), py::arg("l"))
        .def("__repr__", &vec4::to_string);
        //.def("__add__", &vec4::operator+); 
    */
    py::class_<vec4>(m, "vec4")
        // Bind the constructor
        .def(py::init<int, int, int, int>(), py::arg("x"), py::arg("y"), py::arg("t"), py::arg("l"))
        // Bind member functions
        .def("l", &vec4::l)
        .def("t", &vec4::t)
        .def("y", &vec4::y)
        .def("x", &vec4::x)
        .def(py::self == py::self)
        .def(py::self != py::self)
        .def(py::self >= py::self)
        .def(py::self <= py::self)
        .def(py::self > py::self)
        .def(py::self < py::self)
        .def(py::self + py::self)
        .def(py::self - py::self)
        .def(-py::self)
        .def(py::self * int())
        //.def(int() * py::self) //this should be a bug
        .def("to_string", &vec4::to_string)
        // Bind the stream operator for string representation
        .def("__repr__", [](const vec4 &v) { return v.to_string(); });
    py::class_<ext_move>(m, "ext_move")
        .def(py::init<vec4, vec4, piece_t>(),
             py::arg("from"),
             py::arg("to"),
             py::arg("promote_to") = QUEEN_W)
        .def("get_from", &ext_move::get_from)
        .def("get_to", &ext_move::get_to)
        .def("get_promote", &ext_move::get_promote)
        .def("to_string", &ext_move::to_string)
        .def("__repr__", [](const ext_move &m){
            return "<ext_move " + m.to_string() + ">";
        })
        .def(py::self == py::self);
    py::class_<action>(m, "action")
        // No py::init() → Python cannot construct this class
        .def("get_moves", &action::get_moves)
        .def(py::self == py::self)
        // optional: nice repr
        .def("__repr__", [](const action &a){
            return "<action with " + std::to_string(a.get_moves().size()) + " moves>";
        })
    ;
    /*
    py::class_<state>(m, "state")
        .def_readwrite("m", &state::m)
        .def(py::init<multiverse>())
        .def("apply_move", &state::apply_move<false>);
    */
    py::class_<game>(m, "game")
        // metadata
        .def_readwrite("metadata", &game::metadata)
        // factory
        .def_static("from_pgn", &game::from_pgn)
        // core functions
        .def("get_current_state", &game::get_current_state)
        .def("get_current_present", &game::get_current_present)
        .def("get_current_boards", &game::get_current_boards)
        .def("get_current_timeline_status", &game::get_current_timeline_status)
        .def("gen_move_if_playable", &game::gen_move_if_playable)
        .def("get_match_status", &game::get_match_status)
        .def("get_movable_pieces", &game::get_movable_pieces)
        .def("is_playable", &game::is_playable)
        .def("can_undo", &game::can_undo)
        .def("can_redo", &game::can_redo)
        .def("can_submit", &game::can_submit)
        .def("undo", &game::undo)
        .def("redo", &game::redo)
        .def("apply_move", &game::apply_move)
        .def("submit", &game::submit)
        .def("currently_check", &game::currently_check)
        .def("get_current_checks", &game::get_current_checks)
        .def("get_board_size", &game::get_board_size)
        .def("suggest_action", &game::suggest_action)
        .def("perft", [](const game &g, int depth) {
            return g.perft(depth);
        }, py::arg("depth") = 1)
        .def("perft_parallel", [](const game &g, int depth, unsigned int num_threads) {
            return g.perft_parallel(depth, num_threads);
        }, py::arg("depth") = 1, py::arg("num_threads") = 0)
        .def("perft_with_tt", [](const game &g, int depth, unsigned int num_threads, size_t tt_size_mb) {
            return g.perft_with_tt(depth, num_threads, tt_size_mb);
        }, py::arg("depth") = 1, py::arg("num_threads") = 0, py::arg("tt_size_mb") = 256)
        .def("perft_dynamic", [](const game &g, int depth, unsigned int num_threads, int split_depth) {
            return g.perft_dynamic(depth, num_threads, split_depth);
        }, py::arg("depth") = 1, py::arg("num_threads") = 0, py::arg("split_depth") = 2)
        .def("perft_timed", [](const game &g, int depth, double timeout_seconds, unsigned int num_threads) {
            return g.perft_timed(depth, timeout_seconds, num_threads);
        }, py::arg("depth") = 1, py::arg("timeout_seconds") = 60.0, py::arg("num_threads") = 0)
        .def("count_actions", &game::count_actions)
        .def("get_comments", &game::get_comments)
        .def("has_parent", &game::has_parent)
        .def("visit_parent", &game::visit_parent)
        .def("get_child_moves", &game::get_child_moves)
        // Python version of visit_child without newstate argument
        .def("visit_child",
             [](game &g, const action &a) {
                 return g.visit_child(a); 
             },
             py::arg("action")
        )
        .def("show_pgn", &game::show_pgn);
    
    // Bot configuration
    py::class_<bot::BotConfig>(m, "BotConfig")
        .def(py::init<>())
        .def_readwrite("max_depth", &bot::BotConfig::max_depth)
        .def_readwrite("max_nodes", &bot::BotConfig::max_nodes)
        .def_readwrite("time_limit_ms", &bot::BotConfig::time_limit_ms)
        .def_readwrite("use_iterative_deepening", &bot::BotConfig::use_iterative_deepening)
        .def_readwrite("use_transposition_table", &bot::BotConfig::use_transposition_table)
        .def_readwrite("use_move_ordering", &bot::BotConfig::use_move_ordering)
        .def_readwrite("use_late_move_reduction", &bot::BotConfig::use_late_move_reduction)
        .def_readwrite("use_action_sampling", &bot::BotConfig::use_action_sampling)
        .def_readwrite("max_actions_per_ply", &bot::BotConfig::max_actions_per_ply)
        .def_readwrite("beam_width", &bot::BotConfig::beam_width)
        .def_readwrite("tt_size_mb", &bot::BotConfig::tt_size_mb)
        .def_readwrite("num_threads", &bot::BotConfig::num_threads)
        .def_readwrite("use_parallel_search", &bot::BotConfig::use_parallel_search)
        .def_readwrite("verbose", &bot::BotConfig::verbose)
        .def("__repr__", [](const bot::BotConfig& c) {
            return "<BotConfig depth=" + std::to_string(c.max_depth) 
                   + " nodes=" + std::to_string(c.max_nodes)
                   + " threads=" + std::to_string(c.num_threads)
                   + " time=" + std::to_string(c.time_limit_ms) + "ms>";
        });
    
    // Search statistics
    py::class_<bot::SearchStats>(m, "SearchStats")
        .def(py::init<>())
        .def_property_readonly("nodes_searched", &bot::SearchStats::get_nodes_searched)
        .def_property_readonly("tt_hits", &bot::SearchStats::get_tt_hits)
        .def_property_readonly("tt_cutoffs", &bot::SearchStats::get_tt_cutoffs)
        .def_readonly("depth_reached", &bot::SearchStats::depth_reached)
        .def_readonly("time_elapsed_ms", &bot::SearchStats::time_elapsed_ms)
        .def_readonly("best_score", &bot::SearchStats::best_score)
        .def_readonly("search_complete", &bot::SearchStats::search_complete)
        .def("__repr__", [](const bot::SearchStats& s) {
            return "<SearchStats nodes=" + std::to_string(s.get_nodes_searched())
                   + " depth=" + std::to_string(s.depth_reached)
                   + " score=" + std::to_string(s.best_score)
                   + " time=" + std::to_string(s.time_elapsed_ms) + "ms>";
        });
    
    // Bot class
    py::class_<bot::Bot>(m, "Bot")
        .def(py::init<>())
        .def(py::init<bot::BotConfig>(), py::arg("config"))
        .def("find_best_action", [](bot::Bot& b, const game& g) {
            state s = g.get_current_state();
            return b.find_best_action(s);
        }, py::arg("game"))
        .def("find_best_action_from_state", &bot::Bot::find_best_action, py::arg("state"))
        .def("search_with_callback", [](bot::Bot& b, const game& g, 
                                         std::function<void(int, int32_t, std::vector<std::tuple<int,int,int,int,int,int,int,int>>)> callback) {
            state s = g.get_current_state();
            std::function<void(int, bot::score_t, const moveseq&)> cpp_callback = nullptr;
            if (callback) {
                cpp_callback = [callback](int depth, bot::score_t score, const moveseq& pv) {
                    std::vector<std::tuple<int,int,int,int,int,int,int,int>> pv_tuples;
                    for (const auto& m : pv) {
                        pv_tuples.push_back(std::make_tuple(
                            m.from.x(), m.from.y(), m.from.t(), m.from.l(),
                            m.to.x(), m.to.y(), m.to.t(), m.to.l()
                        ));
                    }
                    callback(depth, score, pv_tuples);
                };
            }
            return b.search_with_callback(s, cpp_callback);
        }, py::arg("game"), py::arg("callback") = py::none())
        .def("get_stats", &bot::Bot::get_stats, py::return_value_policy::reference)
        .def("get_config", py::overload_cast<>(&bot::Bot::get_config), py::return_value_policy::reference)
        .def("stop", &bot::Bot::stop)
        .def("__repr__", [](const bot::Bot& b) {
            const auto& stats = b.get_stats();
            return "<Bot nodes=" + std::to_string(stats.nodes_searched)
                   + " depth=" + std::to_string(stats.depth_reached) + ">";
        });
    
    // Evaluator (static methods)
    m.def("evaluate_state", [](const game& g) {
        state s = g.get_current_state();
        return bot::Evaluator::evaluate(s);
    }, py::arg("game"), "Evaluate the current game state");
}

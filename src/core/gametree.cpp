#include "gametree.h"

#include <algorithm>
#include <iterator>

#include "game.h"
#include "pgnparser.h"
#include "variants.h"

std::unique_ptr<gnode<std::vector<std::string>>> build_gametree_from_pgn(std::string input)
{
    auto ag = pgnparser(input).parse_game();
    if(!ag.has_value())
    {
        throw std::runtime_error("Bad input, parse failed");
    }
    pgnparser_ast::gametree &gt_ast = ag->gt;
    variant_setup_t variant_setup = derive_variant_setup(*ag);
    std::unique_ptr<multiverse> m = create_multiverse_from_variant_setup(variant_setup);
    auto gametree = gnode<std::vector<std::string>>::create_root(state(*m), ag->comments);

    std::function<void(gnode<std::vector<std::string>>*, const pgnparser_ast::gametree&)> dfs;
    dfs = [&dfs](gnode<std::vector<std::string>>* node, const pgnparser_ast::gametree& gt_ast) -> void {
        if(std::holds_alternative<pgnparser_ast::gametree::variations_t>(gt_ast.variations_or_outcome))
        {
            const auto& variations = std::get<pgnparser_ast::gametree::variations_t>(gt_ast.variations_or_outcome);
            for(const auto& [act_ast, child_gt] : variations)
            {
                state s = node->get_state();
                std::vector<ext_move> moves;
                for(const auto& mv_ast: act_ast.moves)
                {
                    auto [fm_opt, pt_opt, candidates] = s.parse_move(mv_ast);
                    if(!fm_opt.has_value())
                    {
                        if(candidates.empty())
                        {
                            std::ostringstream oss;
                            oss << "state(): Invalid move: " << mv_ast;
                            throw std::runtime_error(oss.str());
                        }
                        else
                        {
                            std::ostringstream oss;
                            oss << "state(): Ambiguous move: " << mv_ast << "; candidates: ";
                            oss << range_to_string(candidates, "", "");
                            throw std::runtime_error(oss.str());
                        }
                    }
                    full_move fm = fm_opt.value();
                    bool flag;
                    if(pt_opt.has_value())
                    {
                        flag = s.apply_move<false>(fm, *pt_opt);
                    }
                    else
                    {
                        flag = s.apply_move<false>(fm);
                    }
                    if(!flag)
                    {
                        std::ostringstream oss;
                        oss << "state(): Illegal move: " << mv_ast << " (parsed as: " << fm << ")";
                        throw std::runtime_error(oss.str());
                    }
                    moves.push_back(ext_move(fm, pt_opt.has_value() ? *pt_opt : QUEEN_W));
                }
                bool flag = s.submit();
                if(!flag)
                {
                    std::ostringstream oss;
                    oss << "state(): Cannot submit after parsing these moves: " << act_ast;
                    throw std::runtime_error(oss.str());
                }
                action act = action::from_vector(moves, node->get_state());
                std::unique_ptr<gnode<std::vector<std::string>>> child_node =
                    gnode<std::vector<std::string>>::create_child(node, s, act, act_ast.comments);
                gnode<std::vector<std::string>>* child_node_ptr = node->add_child(std::move(child_node));
                dfs(child_node_ptr, *child_gt);
            }
        }
    };

    dfs(gametree.get(), gt_ast);
    return gametree;
}
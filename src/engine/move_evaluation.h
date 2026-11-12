#ifndef MOVE_EVALUATION_H
#define MOVE_EVALUATION_H

#include <optional>
#include <stop_token>
#include <vector>

#include "move_evaluation_weights.h"
#include "move_feature.h"

class HC_info;

using move_score_table = std::vector<std::vector<float>>;

enum class capture_ordering { weighted, mvvlva, mvvlva_exposure, solid };

// Scores physical and arriving HC coordinates; departing and null coordinates
// score zero. The HC must outlive the evaluator and remain unchanged during use.
class move_evaluator
{
    const HC_info &info;
    move_evaluation_weights weights;
    semimove_feature features;
    bool evaluate_captures;
    capture_ordering captures;
    std::vector<int> optional_lines;

public:
    // MVV-LVA uses scaled numerical victim values and a bounded attacker-rank
    // tie-breaker derived from those values at compile time. Royals are only
    // attackers. This replaces the capture-weight contribution; other weights
    // apply normally, including the same check feature and coefficient.
    // The exposure variant penalizes locally attacked, undefended captures and
    // rewards promotion gain, with bounded contributions to keep priority groups.
    move_evaluator(const HC_info &info, move_evaluation_weights weights,
                   capture_ordering captures = capture_ordering::weighted);

    // Solid retains optional moves with a finite expansion-order penalty.
    // Reuses lazily initialized check metadata across coordinates.
    float score(index_t axis, index_t coordinate);

    // Builds table[axis][coordinate], reusing this evaluator's feature context.
    // Pruned coordinate IDs leave zero-filled gaps; cancellation returns nullopt.
    std::optional<move_score_table> build_score_table(std::stop_token stop = {});
};

#endif /* MOVE_EVALUATION_H */

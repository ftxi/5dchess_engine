#ifndef LINEAR_H    
#define LINEAR_H

#include "mcts.h"
#include <array>

/*

*/

class linear_engine : public mcts_engine
{
    constexpr static size_t features_count = 0;
    std::array<float, features_count> weight_vector;
public:
    using mcts_engine::mcts_engine;
    float default_policy(state, std::stop_token, std::mt19937 *) override;
};

#endif /* LINEAR_H */

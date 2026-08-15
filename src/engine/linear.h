#ifndef LINEAR_H    
#define LINEAR_H

#include "mcts.h"

/*
*/

class linear_engine : public mcts_engine
{
public:
    using mcts_engine::mcts_engine;
    float default_policy(state, std::stop_token, std::mt19937 *) override;
}

#endif /* LINEAR_H */

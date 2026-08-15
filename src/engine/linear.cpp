#include "linear.h"
#include "rollout.h"
#include "statistics.h"

float linear_engine::default_policy(state, std::stop_token, std::mt19937 *)
{
    return 0.0f;
}

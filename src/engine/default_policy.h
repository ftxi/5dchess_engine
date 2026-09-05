#ifndef DEFAULT_POLICY_H
#define DEFAULT_POLICY_H

#include <cstdint>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <random>
#include <tuple>

#include "mcts.h"
#include "ordering.h"

constexpr float WINNING_SCORE = 1.0f;

template<class AS>
concept ActionSelection = requires(
    AS &as,
    const state &s,
    std::stop_token stop_token,
    std::mt19937 *rng)
{
    {
        as(s, stop_token, rng)
    } -> std::same_as<std::optional<moveseq>>;
};

template<class CE, typename T>
concept CutoffEvaluation = requires(
    CE &ce,
    state s,
    std::stop_token stop_token,
    std::mt19937 *rng,
    std::size_t rollout_length,
    std::optional<bool> winner
)
{
    {
        ce.cutoff_reward(std::move(s), rollout_length, stop_token, rng)
    } -> std::same_as<std::optional<reward_t<T>>>;
    {
        ce.mate_reward(winner, rollout_length)
    } -> std::same_as<reward_t<T>>;
};

template<typename T, ActionSelection AS, CutoffEvaluation<T> CE>
class default_policy_t
{
    [[no_unique_address]] AS action_selection;
    [[no_unique_address]] CE cutoff_evaluation;
    std::unique_ptr<std::atomic<std::size_t>> rollout_max_actions;
    std::optional<std::mt19937> rng;

    std::mt19937 *rng_pointer()
    {
        return rng.has_value() ? &*rng : nullptr;
    }

public:
    default_policy_t(
        AS as,
        CE ce,
        std::size_t rollout_max_actions = 120,
        std::optional<std::uint32_t> seed = std::nullopt
    ):  action_selection{std::move(as)},
        cutoff_evaluation{std::move(ce)},
        rollout_max_actions{std::make_unique<std::atomic<std::size_t>>(rollout_max_actions)},
        rng {}
    {
        if(seed.has_value())
        {
            rng.emplace(*seed);
        }
    }
    
    void set_max_actions(std::size_t limit) { rollout_max_actions->store(limit); }

    void set_weight_temperature(float temperature)
        requires requires(AS &selection) { selection.set_temperature(temperature); }
    {
        action_selection.set_temperature(temperature);
    }

    void set_rollout_max_actions_option(int limit)
    {
        set_max_actions(static_cast<std::size_t>(std::max(0, limit)));
    }

    void set_weight_temperature_option(double temperature)
        requires requires(AS &selection) { selection.set_temperature(float{}); }
    {
        if(temperature > 0.0 && std::isfinite(temperature)
           && temperature <= std::numeric_limits<float>::max())
        {
            set_weight_temperature(static_cast<float>(temperature));
        }
    }

private:
    static consteval auto make_watched_options()
    {
        auto common = std::tuple{policy_option{
            "rollout-max-actions",
            &default_policy_t::set_rollout_max_actions_option
        }};
        if constexpr(requires(AS &selection) { selection.set_temperature(float{}); })
        {
            return std::tuple_cat(common, std::tuple{policy_option{
                "weight-temperature",
                &default_policy_t::set_weight_temperature_option
            }});
        }
        else
        {
            return common;
        }
    }

public:
    inline constexpr static auto watched_options = make_watched_options();

    using result_type = reward_t<T>;

    template<class Observer>
    std::optional<result_type> evaluate(
        state s,
        std::stop_token stop_token,
        Observer &)
    {
        std::size_t num_actions = 0;
        while(num_actions < rollout_max_actions->load())
        {
            if(stop_token.stop_requested())
            {
                return std::nullopt;
            }

            std::optional<moveseq> action = action_selection(
                s,
                stop_token,
                rng_pointer());
            if(stop_token.stop_requested())
            {
                return std::nullopt;
            }
            if(!action.has_value())
            {
                bool player = s.get_present().second;
                std::optional<bool> winner = std::nullopt;
                if(s.get_mate_type() == mate_type::CHECKMATE)
                {
                    winner = !player;
                }
                return cutoff_evaluation.mate_reward(winner, num_actions);
            }
            for(full_move mv : *action)
            {
                [[maybe_unused]] const bool applied = s.apply_move<true>(mv);
                assert(applied);
            }
            s.submit<true>();
            num_actions++;
        }
        return cutoff_evaluation.cutoff_reward(
            std::move(s),
            num_actions,
            stop_token,
            rng_pointer());
    }
};

#endif /* DEFAULT_POLICY_H */

#include "ordering.h"
#include <cassert>

random_HC_ordering::random_HC_ordering(const HC &hc, std::mt19937 &rng)
{
    orderings.resize(hc.dimension());
    for(index_t n = 0; n < hc.dimension(); n++)
    {
        orderings[n].assign(hc[n].begin(), hc[n].end());
        std::shuffle(orderings[n].begin(), orderings[n].end(), rng);
    }
}

scored_HC_ordering::scored_HC_ordering(const HC &hc, const std::vector<std::vector<float>> &scores, std::mt19937 &rng)
{
    assert(hc.dimension() == scores.size());
    orderings.resize(hc.dimension());
    for(index_t n = 0; n < hc.dimension(); n++)
    {
        orderings[n].assign(hc[n].begin(), hc[n].end());
        std::shuffle(orderings[n].begin(), orderings[n].end(), rng);
        std::sort(orderings[n].begin(), orderings[n].end(), [&](index_t i, index_t j) {
            return scores[n][i] > scores[n][j];
        });
    }
}

weighted_HC_ordering::weighted_HC_ordering(const HC &hc, std::vector<std::vector<float>> weights, std::mt19937 &rng)
{
    assert(hc.dimension() == weights.size());
    // initialize orderings as deep copy of hc
    orderings.resize(hc.dimension());
    for(index_t n = 0; n < hc.dimension(); n++)
    {
        orderings[n].assign(hc[n].begin(), hc[n].end());
    }
    // shuffle each axis according to weights
    for(index_t n = 0; n < hc.dimension(); n++)
    {
        for(std::size_t position = 0; position < orderings[n].size(); ++position)
        {
            // Draw one of the as-yet unplaced values, then keep its
            // weight paired with it while moving both into place.
            std::discrete_distribution<std::size_t> dist(
                weights[n].begin() + position, weights[n].end());
            const std::size_t selected = position + dist(rng);
            std::swap(orderings[n][position], orderings[n][selected]);
            std::swap(weights[n][position], weights[n][selected]);
        }
    }
}

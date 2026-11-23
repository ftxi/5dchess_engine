#include "structural_probe.h"
#include "graph.h"
#include "hypercuboid.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <string_view>
#include <tuple>

namespace structural_probe
{
namespace
{
using clock = std::chrono::steady_clock;
constexpr auto missing = std::numeric_limits<index_t>::max();

struct conflict
{
    unsigned first, second;
    integer_set first_values, second_values;
};

struct edge
{
    index_t first = missing, second = missing;
};
} /* anonymous namespace */

struct model::implementation
{
    using signature = std::tuple<kind, unsigned, unsigned, board_key, board_key, int>;
    axes options;
    std::vector<std::vector<index_t>> translate;
    std::vector<integer_set> nonjump;
    std::vector<conflict> conflicts;
    std::vector<HC> failures;
    graph matching;
    graph::workspace matching_scratch;
    std::vector<edge> edges;
    std::vector<index_t> required;

    implementation(const axes &coordinates, unsigned first_branch, bool enforce_order)
        : options(coordinates.size()), translate(coordinates.size()), nonjump(coordinates.size()),
          matching(static_cast<index_t>(coordinates.size())), edges(coordinates.size() * coordinates.size())
    {
        const unsigned count = coordinates.size();
        for(unsigned axis = 0; axis < count; ++axis)
        {
            std::map<signature, index_t> groups;
            for(auto c : coordinates[axis])
            {
                auto &map = translate[axis];
                if(map.size() <= c.value) map.resize(c.value + 1, missing);
                const signature key{c.type, c.departure_axis,
                    c.type == kind::departure ? c.value : c.departure_value,
                    c.from, c.to, c.playable_endpoint};
                auto [entry, inserted] = groups.try_emplace(key, options[axis].size());
                map[c.value] = entry->second;
                if(inserted) options[axis].push_back(c);
            }
        }

        std::vector<integer_set> nulls(count), arrivals(count);
        std::map<board_key, std::map<unsigned, integer_set>> sources, destinations;
        std::map<std::pair<unsigned, unsigned>, integer_set> endpoints;
        for(unsigned axis = 0; axis < count; ++axis)
            for(unsigned value = 0; value < options[axis].size(); ++value)
            {
                auto &entry = options[axis][value];
                switch(entry.type)
                {
                case kind::null: nulls[axis].insert(value); [[fallthrough]];
                case kind::physical: nonjump[axis].insert(value); break;
                case kind::departure: break;
                case kind::arrival:
                    if(axis < first_branch) break;
                    arrivals[axis].insert(value);
                    if(entry.playable_endpoint >= 0)
                        endpoints[{axis, entry.playable_endpoint}].insert(value);
                    if(enforce_order)
                    {
                        sources[entry.from][axis].insert(value);
                        destinations[entry.to][axis].insert(value);
                    }
                    break;
                }
            }

        const auto add = [&](unsigned first, unsigned second, const integer_set &a, const integer_set &b) {
            if(!a.empty() && !b.empty()) conflicts.push_back({first, second, a, b});
        };
        for(unsigned axis = first_branch + 1; axis < count; ++axis)
            add(axis - 1, axis, nulls[axis - 1], arrivals[axis]);
        for(const auto &[pair, values] : endpoints)
            add(pair.first, pair.second, values, nulls[pair.second]);
        for(const auto &[board, outgoing] : sources)
        {
            const auto incoming = destinations.find(board);
            if(incoming == destinations.end()) continue;
            for(const auto &[earlier, arrivals] : incoming->second)
                for(auto later = outgoing.upper_bound(earlier); later != outgoing.end(); ++later)
                    add(earlier, later->first, arrivals, later->second);
        }
    }

    bool propagate(HC &domain, std::span<const conflict *const> active) const
    {
        bool changed;
        do
        {
            changed = false;
            for(const auto *c : active)
            {
                auto &a = domain[c->first], &b = domain[c->second];
                if(!a.intersects(c->first_values) || !b.intersects(c->second_values)) continue;
                if(a.is_subset_of(c->first_values))
                {
                    b.minus(c->second_values);
                    if(b.empty()) return false;
                    changed = true;
                }
                else if(b.is_subset_of(c->second_values))
                {
                    a.minus(c->first_values);
                    if(a.empty()) return false;
                    changed = true;
                }
            }
        } while(changed);
        return true;
    }

    bool pick_matching(const HC &domain, point &selected)
    {
        const unsigned count = options.size();
        matching.reset(static_cast<index_t>(count));
        std::fill(edges.begin(), edges.end(), edge{});
        required.clear();
        for(unsigned axis = 0; axis < count; ++axis)
        {
            selected[axis] = missing;
            nonjump[axis].for_each_intersection(domain[axis], [&](index_t value) {
                if(selected[axis] == missing) selected[axis] = value;
            });
            if(selected[axis] == missing) required.push_back(axis);

            for(auto value : domain[axis])
            {
                const auto &entry = options[axis][value];
                if(entry.type != kind::arrival) continue;
                const unsigned source = entry.departure_axis, departure = entry.departure_value;
                if(source >= count || departure >= translate[source].size()) continue;
                const auto partner = translate[source][departure];
                if(partner == missing || !domain[source].contains(partner)) continue;
                assert(axis != source);
                const unsigned first = std::min(source, axis), second = std::max(source, axis);
                auto &connection = edges[first * count + second];
                if(connection.first != missing) continue;
                matching.add_edge(first, second);
                connection = source == first ? edge{partner, value} : edge{value, partner};
            }
        }
        const auto witness = matching.find_matching(required, matching_scratch);
        if(!witness) return false;
        for(const auto &[a, b] : *witness)
        {
            const auto first = std::min(a, b), second = std::max(a, b);
            const auto connection = edges[first * count + second];
            selected[first] = connection.first;
            selected[second] = connection.second;
        }
        return true;
    }

    result check(const HC &raw, clock::time_point deadline)
    {
        std::vector<integer_set> compressed(options.size());
        for(unsigned axis = 0; axis < options.size(); ++axis)
        {
            for(auto value : raw[axis])
                compressed[axis].insert(translate[axis][value]);
            if(compressed[axis].empty()) return result::unsat;
        }
        HC domain(std::move(compressed));
        for(const auto &failure : failures)
        {
            bool subset = true;
            for(unsigned axis = 0; axis < options.size() && subset; ++axis)
                subset = domain[axis].is_subset_of(failure[axis]);
            if(subset) return result::unsat;
        }

        std::vector<const conflict *> active;
        for(const auto &c : conflicts)
            if(domain[c.first].intersects(c.first_values) && domain[c.second].intersects(c.second_values))
                active.push_back(&c);

        point selected(options.size());
        std::vector<HC> pending{domain};
        while(!pending.empty())
        {
            if(clock::now() >= deadline) return result::unknown;
            HC box = std::move(pending.back());
            pending.pop_back();
            if(!propagate(box, active) || !pick_matching(box, selected)) continue;

            const conflict *violated = nullptr;
            double most_excluded = 0;
            for(const auto *c : active)
                if(c->first_values.contains(selected[c->first]) && c->second_values.contains(selected[c->second]))
                {
                    const auto &a = box[c->first], &b = box[c->second];
                    const double excluded = double((a & c->first_values).size())
                        * double((b & c->second_values).size()) / (double(a.size()) * double(b.size()));
                    if(excluded > most_excluded)
                    {
                        violated = c;
                        most_excluded = excluded;
                    }
                }
            if(!violated) return result::sat;

            // Every admissible assignment either avoids the first value set
            // or uses it and avoids the second. Explore those two disjoint boxes.
            HC other = box;
            other[violated->first] &= violated->first_values;
            other[violated->second].minus(violated->second_values);
            box[violated->first].minus(violated->first_values);
            pending.push_back(std::move(other));
            pending.push_back(std::move(box));
        }
        if(failures.size() == 32) failures.erase(failures.begin());
        failures.push_back(std::move(domain));
        return result::unsat;
    }
};

model::model(const axes &coordinates, unsigned first_branch, bool enforce_order)
    : impl(std::make_unique<implementation>(coordinates, first_branch, enforce_order)) {}
model::~model() = default;

result model::check(const HC &domain, unsigned timeout_ms)
{
    return impl->check(domain, clock::now() + std::chrono::milliseconds(timeout_ms));
}
} /* namespace structural_probe */
std::vector<std::vector<structural_probe::coordinate>> HC_info::structural_coordinates() const
{
    using namespace structural_probe;
    axes result(dimension());
    for(index_t axis = 0; axis < dimension(); ++axis)
        for(auto value : universe[axis])
        {
            coordinate entry{value, kind::physical};
            if(const auto *arrival = std::get_if<arriving_entry>(&axis_coords[axis][value]))
            {
                entry.type = kind::arrival;
                entry.departure_axis = line_to_axis_unsafe(arrival->m.from.l());
                entry.departure_value = arrival->idx;
                entry.from = {arrival->m.from.l(), arrival->m.from.t()};
                entry.to = {arrival->m.to.l(), arrival->m.to.t()};
                const auto target = line_to_axis_unsafe(entry.to.timeline);
                if(target != no_axis && target < new_axis
                   && s.get_timeline_end(entry.to.timeline) == turn{entry.to.time, s.get_present().c()})
                    entry.playable_endpoint = target;
            }
            else if(departing_entries[axis].contains(value)) entry.type = kind::departure;
            else if(value == null_coordinate) entry.type = kind::null;
            result[axis].push_back(entry);
        }
    return result;
}

bool HC_info::run_structural_probe(completion_space &space, const HC &domain) const
{
    using namespace structural_probe;
    using clock = std::chrono::steady_clock;
    static const char *mode = std::getenv("FINE_STRUCTURAL_MODE");
    if(!mode || std::string_view(mode) == "trace") return false;
    static const unsigned budget = [] {
        const auto *value = std::getenv("FINE_STRUCTURAL_MS");
        return value ? static_cast<unsigned>(std::strtoul(value, nullptr, 10)) : 150u;
    }();
    if(!budget) return false;
    const auto start = clock::now();
    const bool fresh = std::string_view(mode) == "fresh";
    const bool cold = fresh || !space.structural_model;
    auto local = space.structural_model;
    if(cold) local = std::make_shared<model>(structural_coordinates(), new_axis);
    if(!fresh) space.structural_model = local;
    const auto setup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count();
    const auto answer = setup_ms < budget ? local->check(domain, budget - static_cast<unsigned>(setup_ms)) : result::unknown;
    const auto elapsed = std::chrono::duration<double, std::milli>(clock::now() - start).count();
    std::fprintf(stderr, "STRUCTURAL %zu %d %s %lld %.3f\n", space.witness_calls, int(cold),
        answer == result::unsat ? "unsat" : answer == result::sat ? "sat" : "unknown",
        static_cast<long long>(setup_ms), elapsed);
    return answer == result::unsat;
}

void HC_info::trace_structural_query(completion_space &space, const HC &domain) const
{
    if(!std::getenv("FINE_STRUCTURAL_TRACE")) return;
    static std::atomic_size_t contexts{0};
    if(space.structural_trace_id == 0)
    {
        space.structural_trace_id = ++contexts;
        const auto coordinates = structural_coordinates();
        std::fprintf(stderr, "PROBLEM %zu %u %u\n", space.structural_trace_id,
            unsigned(dimension()), unsigned(new_axis));
        for(unsigned axis = 0; axis < coordinates.size(); ++axis)
            for(const auto &c : coordinates[axis])
                std::fprintf(stderr, "CO %u %u %d %u %u %d %d %d %d %d\n", axis, c.value,
                    int(c.type), c.departure_axis, c.departure_value, c.from.timeline, c.from.time,
                    c.to.timeline, c.to.time, c.playable_endpoint);
    }
    std::fprintf(stderr, "QUERY %zu %zu\n", space.structural_trace_id, space.witness_calls);
    for(index_t axis = 0; axis < dimension(); ++axis)
        if(!universe[axis].is_subset_of(domain[axis]))
        {
            std::fprintf(stderr, "ALLOW %u", unsigned(axis));
            for(auto value : domain[axis]) std::fprintf(stderr, " %u", unsigned(value));
            std::fputc('\n', stderr);
        }
    std::fputs("ENDQUERY\n", stderr);
}

#include "tier1_v2/function_timing.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <print>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {
    // A recorder sits at the top of its function, so its site identifies the function. Names alone are ambiguous:
    // static functions in different files share them.
    using SiteKey = std::tuple<std::string_view, uint32_t, uint32_t>;

    struct FunctionTotal {
        const vstd::FunctionTiming *first; // name and stage
        uint64_t calls{0};
        double own_duration{0};
    };

    // 3 decimals in the largest unit that keeps the value >= 1
    std::string format_duration(const double milliseconds) {
        if (milliseconds >= 1000.0)
            return std::format("{:.03f}s", milliseconds / 1000.0);
        if (milliseconds >= 1.0)
            return std::format("{:.03f}ms", milliseconds);
        return std::format("{:.03f}us", milliseconds * 1000.0);
    }

    void print_tree() {
        for (const auto &timing: vstd::function_timings.rows) {
            if (timing.duration < 0.1) // Skip too short timings
                continue;
            for (uint32_t i = 0; i < timing.depth; ++i)
                std::print("  ");
            std::print("[{}]: {}: ", timing.stage_name, timing.function_name);
            const auto duration = timing.duration;
            if (duration < 1000.0)
                std::print("calls: {}, own: {:.03f}ms, total: {:.03f}ms\n", timing.calls, timing.own_duration,
                           duration);
            else
                std::print("calls: {}, own: {:.03f}s, total: {:.03f}s\n", timing.calls, timing.own_duration / 1000.0,
                           duration / 1000.0);
        }
    }

    // Every row of a function (it appears once per distinct caller path) merged into one line,
    // sorted by own time per call
    void print_per_function() {
        std::map<SiteKey, FunctionTotal> totals;
        for (const auto &timing: vstd::function_timings.rows) {
            const SiteKey key{timing.location.file_name(), timing.location.line(), timing.location.column()};
            auto &total = totals.try_emplace(key, FunctionTotal{&timing}).first->second;
            total.calls += timing.calls;
            total.own_duration += timing.own_duration;
        }

        std::vector<const FunctionTotal *> sorted;
        sorted.reserve(totals.size());
        size_t name_width = 0;
        for (const auto &[key, total]: totals) {
            if (total.own_duration < 0.1) // same cut-off as the tree, on the combined own time
                continue;
            sorted.push_back(&total);
            name_width = std::max(name_width, total.first->stage_name.size() + total.first->function_name.size() + 3);
        }
        std::ranges::sort(sorted, [](const FunctionTotal *lhs, const FunctionTotal *rhs) {
            return lhs->own_duration / static_cast<double>(lhs->calls) >
                   rhs->own_duration / static_cast<double>(rhs->calls);
        });

        std::print("\nPer function, by own time per call:\n");
        for (const FunctionTotal *total: sorted) {
            const std::string name = std::format("[{}] {}", total->first->stage_name, total->first->function_name);
            const double per_call = total->own_duration / static_cast<double>(total->calls);
            std::print("  {:<{}}  own/call: {:>10}, calls: {:>7}, own: {:>10}\n", name, name_width,
                       format_duration(per_call), total->calls, format_duration(total->own_duration));
        }
    }
}

void vstd::print_function_timings() {
    print_tree();
    print_per_function();
}

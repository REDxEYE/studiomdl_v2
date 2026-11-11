//
// Per-function wall-clock timings. Scope a recorder at the top of a function:
//     vstd::FunctionTimingRecorder _("Stage");
// and print the call tree with vstd::print_function_timings().
//
// Single-threaded: recorders must be created and destroyed on one thread, in stack order.
//

#pragma once

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace vstd {
    struct FunctionTiming {
        std::source_location location;
        std::string function_name;
        std::string stage_name;
        uint32_t depth;
        uint32_t calls{1};
        double duration{0}; // milliseconds, including nested recorders
        double own_duration{0}; // milliseconds, excluding nested recorders
        uint32_t active{0}; // calls in progress
    };

    // Rows in call order; a row's children follow it with a larger indent
    struct FunctionTimings {
        std::vector<FunctionTiming> rows;
        uint32_t depth{0};
    };

    // inline: code in any library can record without linking the printer's translation unit
    inline FunctionTimings function_timings;

    // Prints rows of at least 0.1 ms as an indented tree to stdout (libs/tier1/function_timing.cpp)
    void print_function_timings();

    // Records the call into function_timings. Consecutive calls of the same site at the same nesting level
    // merge into one row; own time is the total minus the totals of the timed calls made directly from this one.
    // recursive: a call made while a call of the same site is live (directly or through other recorders) is folded
    // into that call's row: it adds its call and own time, not its duration (the live call's total contains it),
    // and its children are recorded one level below that row.
    class FunctionTimingRecorder {
    public:
        explicit FunctionTimingRecorder(const std::string_view stage_name,
                                        const bool recursive = false,
                                        const std::source_location location = std::source_location::current()
        )
            : parent(current), saved_depth(function_timings.depth) {
            auto &timings = function_timings;
            assert(timings.depth < 1000);

            auto &rows = timings.rows;
            if (recursive) {
                for (const FunctionTimingRecorder *pAncestor = parent; pAncestor; pAncestor = pAncestor->parent) {
                    if (same_site(rows[pAncestor->index].location, location)) {
                        index = pAncestor->index;
                        folded = true;
                        break;
                    }
                }
            }

            if (folded) {
                rows[index].calls++;
                timings.depth = rows[index].depth + 1;
            }
            // Only the last row can be extended, and only when it is a sibling: a row at a shallower level is an
            // enclosing call (recursion), and a row at a deeper level belongs to another call's subtree.
            // (A folded call places its children at the folded row's level, next to rows of calls still in progress:
            // those are never extended, their totals would count the new call twice.)
            else if (!rows.empty() && rows.back().depth == timings.depth && rows.back().active == 0 &&
                     same_site(rows.back().location, location)) {
                index = rows.size() - 1;
                rows.back().calls++;
                timings.depth++;
            }
            else {
                index = rows.size();
                rows.emplace_back(location, std::string(clean_function_name(location)), std::string(stage_name),
                                  timings.depth);
                timings.depth++;
            }
            rows[index].active++;
            current = this;
            start = std::chrono::steady_clock::now();
        }

        ~FunctionTimingRecorder() {
            const double duration =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            current = parent;
            function_timings.depth = saved_depth;
            if (parent)
                parent->children_duration += duration;

            auto &timing = function_timings.rows[index];
            timing.active--;
            if (!folded)
                timing.duration += duration;
            timing.own_duration += duration - children_duration;
        }

        FunctionTimingRecorder(const FunctionTimingRecorder &) = delete;

        FunctionTimingRecorder &operator=(const FunctionTimingRecorder &) = delete;

    private:
        // compare text, not pointers: an inline function's literals may live at different addresses per TU
        static bool same_site(const std::source_location &lhs, const std::source_location &rhs) {
            return lhs.line() == rhs.line() && lhs.column() == rhs.column() &&
                   std::string_view(lhs.file_name()) == rhs.file_name() &&
                   std::string_view(lhs.function_name()) == rhs.function_name();
        }

        static std::string_view clean_function_name(const std::source_location &loc) {
            std::string_view name = loc.function_name();
            if (const auto open_paren = name.find('('); open_paren != std::string_view::npos) {
                name = name.substr(0, open_paren);
            }
            if (const auto last_space = name.rfind(' '); last_space != std::string_view::npos) {
                name = name.substr(last_space + 1);
            }

            return name;
        }

        static inline FunctionTimingRecorder *current = nullptr; // innermost live recorder

        FunctionTimingRecorder *parent;
        uint32_t saved_depth; // function_timings.depth before this call
        std::chrono::steady_clock::time_point start;
        double children_duration{0};
        size_t index;
        bool folded{false}; // recursive call counted in a live ancestor's row
    };
}

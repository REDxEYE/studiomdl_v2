#pragma once

#include <cstddef>
#include <cstdlib>

namespace vstd {
    // qsort whose comparator gets a context pointer: the platform's qsort_r / qsort_s, which use the same algorithm as
    // the platform's qsort, so element order (including ties) is exactly what qsort produced. Use this instead of a
    // comparator that reads global state; std::sort orders ties differently.
    using QSortContextCompare = int (*)(const void *a, const void *b, void *context);

    namespace detail {
#if defined(_WIN32) || defined(__APPLE__)
        struct QSortThunk {
            QSortContextCompare compare;
            void *context;
        };

#if defined(_WIN32)
        inline int __cdecl qsort_s_thunk(void *thunk, const void *a, const void *b) {
#else
        // BSD / Apple qsort_r: the context comes first in the comparator
        inline int qsort_s_thunk(void *thunk, const void *a, const void *b) {
#endif
            const auto *t = static_cast<const QSortThunk *>(thunk);
            return t->compare(a, b, t->context);
        }
#endif
    }

    inline void qsort_with_context(void *base, size_t count, size_t size, QSortContextCompare compare, void *context) {
#if defined(_WIN32)
        detail::QSortThunk thunk{compare, context};
        qsort_s(base, count, size, detail::qsort_s_thunk, &thunk);
#elif defined(__APPLE__)
        detail::QSortThunk thunk{compare, context};
        qsort_r(base, count, size, &thunk, detail::qsort_s_thunk);
#else
        qsort_r(base, count, size, compare, context);
#endif
    }
}

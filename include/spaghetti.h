#pragma once

#include <cstdint>
#include <cassert>
#include <iostream>
#include <cstdarg>
#include <sal.h>
#include <intrin.h>

#define FORCEINLINE inline
#define RESTRICT
#define ALIGN16 alignas(16)
#define abstract_class class NO_VTABLE
#define V_memset memset
#define Assert assert


inline int GetHardwareClockFast() {
    return __rdtsc();
}

void Warning(const char *format, ...) {
    va_list args;
            va_start(args, format);
    std::vfprintf(stderr, format, args);
            va_end(args);
    std::fprintf(stderr, "\n");
}

inline void AssertValidStringPtr(const tchar *ptr, int maxchar = 0xFFFFFF) {}

template<class T>
inline void AssertValidReadPtr(T *ptr, int count = 1) {}

template<class T>
inline void AssertValidWritePtr(T *ptr, int count = 1) {}

template<class T>
inline void AssertValidReadWritePtr(T *ptr, int count = 1) {}

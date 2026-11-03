#pragma once

// Minimal compatibility layer for the copy of the legacy QHull sources that
// shipped inside IVP.  Keep this intentionally independent from IVP/VPhysics:
// the QHull code only relied on IVP for allocator/string wrappers and logging.

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _MSC_VER
#ifndef __cdecl
#define __cdecl
#endif
#endif

#ifndef p_malloc
#define p_malloc(sz) malloc(sz)
#endif
#ifndef p_calloc
#define p_calloc(n,sz) calloc((n),(sz))
#endif
#ifndef p_strlen
#define p_strlen(s) strlen(s)
#endif
#ifndef P_FREE
#define P_FREE(p) do { if ((p) != NULL) { free((void *)(p)); (p) = NULL; } } while (0)
#endif

static inline void legacy_qhull_message( const char *fmt, ... )
{
    va_list args;
    va_start( args, fmt );
    vfprintf( stderr, fmt, args );
    va_end( args );
}

#ifndef ivp_message
#define ivp_message legacy_qhull_message
#endif

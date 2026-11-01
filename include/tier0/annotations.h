#pragma once

// MSVC /analyze (SAL) annotations used by the code base; they expand to nothing in these builds.
#define ANALYZE_SUPPRESS(wnum)
#define PRINTF_FORMAT_STRING
#define SCANF_FORMAT_STRING
#define IN_Z
#define OUT_Z_CAP(x)
#define OUT_CAP(x)
#define OUT_Z_BYTECAP(x)
#define INOUT_Z_CAP(x)
#define INOUT_Z_BYTECAP(x)
#define OUT_Z_ARRAY
#define INOUT_Z_ARRAY


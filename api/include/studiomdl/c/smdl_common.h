/* studiomdl C API: common definitions shared by every part (STUDIOMDL_API_PLAN.md, "C ABI conventions").
 *
 * - Strings are UTF-8 and copied during the call.
 * - Arrays are (const T *data, size_t count, size_t stride_bytes); stride 0 means tightly packed. Data is copied.
 * - Vectors are const float[3], quaternions const float[4] (x, y, z, w).
 * - Every function returns smdl_status; smdl_model_last_error() explains the last failure on that model.
 * - Handles created from a model are owned by it and valid until smdl_model_destroy.
 */
#ifndef STUDIOMDL_C_SMDL_COMMON_H
#define STUDIOMDL_C_SMDL_COMMON_H

#include <stddef.h>
#include <stdint.h>

#if defined(SMDL_STATIC)
#  define SMDL_API
#elif defined(_WIN32)
#  if defined(SMDL_BUILDING)
#    define SMDL_API __declspec(dllexport)
#  else
#    define SMDL_API __declspec(dllimport)
#  endif
#else
#  define SMDL_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SMDL_API_VERSION 1u

typedef enum smdl_status {
    SMDL_OK = 0,
    SMDL_ERROR_INVALID_ARGUMENT = 1, /* null handle/pointer, bad enum value, wrong array length, NaN */
    SMDL_ERROR_OUT_OF_RANGE = 2,     /* id or index not valid for this model/geometry */
    SMDL_ERROR_DUPLICATE_NAME = 3,   /* a name already used by another object of the same kind */
    SMDL_ERROR_NOT_FOUND = 4,        /* name lookup found nothing */
    SMDL_ERROR_LIMIT = 5,            /* over a studiomdl/MDL format limit (MAXSTUDIO*) */
    SMDL_ERROR_SYNTAX = 6,           /* flex rule expression does not parse */
    SMDL_ERROR_COMPILE = 7,          /* the compile failed: see smdl_result_error_* */
    SMDL_ERROR_IO = 8,               /* a file could not be written (save) */
    SMDL_ERROR_INTERNAL = 9          /* unexpected exception, out of memory */
} smdl_status;

/* Index ids (typedefs only: they document intent, the call-time checks catch wrong values) */
typedef uint32_t smdl_bone_id;
typedef uint32_t smdl_material_id;        /* per geometry */
typedef uint32_t smdl_pose_param_id;
typedef uint32_t smdl_flex_controller_id;
typedef uint32_t smdl_flex_id;
typedef uint32_t smdl_ik_chain_id;
typedef uint32_t smdl_weight_list_id;
typedef uint32_t smdl_attachment_id;
typedef uint32_t smdl_hitbox_set_id;

#define SMDL_NO_BONE ((smdl_bone_id)UINT32_MAX)
#define SMDL_NO_ID UINT32_MAX

/* Opaque handles. smdl_model and smdl_result are created/destroyed by the host; the rest belong to their model. */
typedef struct smdl_model smdl_model;
typedef struct smdl_result smdl_result;
typedef struct smdl_geometry smdl_geometry;
typedef struct smdl_mesh smdl_mesh;
typedef struct smdl_bodygroup smdl_bodygroup;
typedef struct smdl_animation smdl_animation;
typedef struct smdl_sequence smdl_sequence;
typedef struct smdl_lod smdl_lod;
typedef struct smdl_collision smdl_collision;

typedef enum smdl_severity { SMDL_SEVERITY_MESSAGE = 0, SMDL_SEVERITY_WARNING = 1, SMDL_SEVERITY_ERROR = 2 } smdl_severity;

/* Receives every compiler message during smdl_model_compile (nothing goes to stdout). text is one complete message. */
typedef void (*smdl_log_fn)(void *user, smdl_severity severity, const char *text);

SMDL_API uint32_t smdl_api_version(void);
SMDL_API const char *smdl_status_string(smdl_status status);

#ifdef __cplusplus
}
#endif

#endif

/* studiomdl C API: model lifetime and errors */
#ifndef STUDIOMDL_C_SMDL_MODEL_H
#define STUDIOMDL_C_SMDL_MODEL_H

#include "smdl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* name: the model path relative to models/, without extension ("props/crate" -> models/props/crate.mdl).
 * Returns NULL if name is NULL or empty. */
SMDL_API smdl_model *smdl_model_create(const char *name);
SMDL_API void smdl_model_destroy(smdl_model *model);

/* Explanation of the last failed call on this model ("" if none). Valid until the next call on the model. */
SMDL_API const char *smdl_model_last_error(const smdl_model *model);

SMDL_API smdl_status smdl_model_set_name(smdl_model *model, const char *name);
SMDL_API const char *smdl_model_get_name(const smdl_model *model);

#ifdef __cplusplus
}
#endif

#endif

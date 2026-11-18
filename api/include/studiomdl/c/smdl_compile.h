/* studiomdl C API: compile and results */
#ifndef STUDIOMDL_C_SMDL_COMPILE_H
#define STUDIOMDL_C_SMDL_COMPILE_H

#include "smdl_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compiles the model. One compile runs at a time per process; a second call waits for the first.
 * game_dir: directory of gameinfo.txt, or NULL for a pure in-memory compile (then $includemodel, the game's
 * surfaceproperties and the sequence remap check are unavailable). log: optional message callback.
 * *out_result is always set (also on failure, for the error details) unless out_result is NULL; destroy it with
 * smdl_result_destroy. Returns SMDL_OK or SMDL_ERROR_COMPILE (details in the result), or an argument error. */
SMDL_API smdl_status smdl_model_compile(const smdl_model *model, const char *game_dir, smdl_log_fn log, void *user,
                                        smdl_result **out_result);

SMDL_API void smdl_result_destroy(smdl_result *result);

SMDL_API smdl_status smdl_result_status(const smdl_result *result);

/* Output files: relative_path is "models/<name>.mdl" etc. (forward slashes). Pointers valid until destroy. */
SMDL_API size_t smdl_result_file_count(const smdl_result *result);
SMDL_API smdl_status smdl_result_file(const smdl_result *result, size_t index, const char **relative_path,
                                      const void **data, size_t *size);

/* Writes every output file to <dir>/<relative_path>, creating directories. save and save_to are the same operation:
 * save is meant for a game directory, save_to for any other root (both give <root>/models/<name>.*). */
SMDL_API smdl_status smdl_result_save(const smdl_result *result, const char *game_dir);
SMDL_API smdl_status smdl_result_save_to(const smdl_result *result, const char *dir);

SMDL_API int smdl_result_warning_count(const smdl_result *result);

/* Every message of the compile, in order (the same texts the log callback received) */
SMDL_API size_t smdl_result_message_count(const smdl_result *result);
SMDL_API smdl_status smdl_result_message(const smdl_result *result, size_t index, smdl_severity *severity,
                                         const char **text);

/* On failure: the error message, and the object being applied when it happened ("bodygroup 'lid' / mesh 'a'"),
 * "" when unknown. NULL / "" on success. */
SMDL_API const char *smdl_result_error_message(const smdl_result *result);
SMDL_API const char *smdl_result_error_object(const smdl_result *result);

/* QC compiles (smdl_qc.h): the script file and line the error was reported at ("" / 0 when unknown or not a script
 * error). */
SMDL_API const char *smdl_result_error_file(const smdl_result *result);
SMDL_API int smdl_result_error_line(const smdl_result *result);

#ifdef __cplusplus
}
#endif

#endif

/* studiomdl C API: compiling an existing QC (or .mpp) file with the command line's semantics.
 *
 *   smdl_qc_compile *c = smdl_qc_compile_create();
 *   smdl_qc_compile_set_game_dir(c, "/games/mymod");          // optional, as for the model builder
 *   smdl_qc_compile_set_qc_path(c, "/content/props/crate.qc");
 *   smdl_qc_compile_set_flex_optimize(c, SMDL_QC_FLEXOPT_ON);  // typed switch ...
 *   smdl_qc_compile_add_argument(c, "-maxwarnings");          // ... or any other switch, one call per argument
 *   smdl_qc_compile_add_argument(c, "10");
 *   smdl_qc_compile_add_file(c, "/content/props/crate.smd", data, size);   // a source that is not on the disk
 *   smdl_result *r;
 *   smdl_qc_compile_run(c, &r);                                // same result object as smdl_model_compile
 *
 * The result is the builder's (smdl_compile.h): files in memory (relative_path "models/props/crate.mdl"), messages,
 * warning count and, on failure (SMDL_ERROR_COMPILE), the error message plus the QC file and line
 * (smdl_result_error_file / smdl_result_error_line). Nothing is written to stdout, the working directory is not
 * changed and relative paths in the QC resolve against the QC's directory. One compile runs at a time per process
 * (shared with smdl_model_compile). -makefile is not supported.
 */
#ifndef STUDIOMDL_C_SMDL_QC_H
#define STUDIOMDL_C_SMDL_QC_H

#include "smdl_common.h"
#include "smdl_compile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct smdl_qc_compile smdl_qc_compile;

typedef enum smdl_qc_flex_optimize {
    SMDL_QC_FLEXOPT_AUTO = 0, /* -flexopt auto (the default) */
    SMDL_QC_FLEXOPT_ON = 1,
    SMDL_QC_FLEXOPT_OFF = 2
} smdl_qc_flex_optimize;

/* The host's file reader (smdl_qc_compile_set_reader). Called for a path the compiler wants to read before the disk is
 * asked; path is absolute, UTF-8, with '/' separators and normalized (no "." / ".."; lower case on Windows). Return
 * non-zero and set *data / *size to provide the file (the bytes are copied before the call returns), or 0 for "not
 * mine": the file comes from the disk. Called at most once per path per compile. Must not call back into the library. */
typedef int (*smdl_qc_read_fn)(void *user, const char *path, const void **data, size_t *size);

SMDL_API smdl_qc_compile *smdl_qc_compile_create(void);
SMDL_API void smdl_qc_compile_destroy(smdl_qc_compile *compile);
/* The message of the last failed call on this handle ("" when it succeeded) */
SMDL_API const char *smdl_qc_compile_last_error(const smdl_qc_compile *compile);

/* Directory of gameinfo.txt (search paths, $includemodel, surfaceproperties), or NULL for none (a virtual game
 * directory: the outputs live in memory only). gameinfo.txt may come from add_file / the reader. */
SMDL_API smdl_status smdl_qc_compile_set_game_dir(smdl_qc_compile *compile, const char *game_dir);

/* The .qc or .mpp file to compile (relative paths are made absolute against the current directory now) */
SMDL_API smdl_status smdl_qc_compile_set_qc_path(smdl_qc_compile *compile, const char *qc_path);

/* Typed switches: each is the command line switch of the same name (the same parser handles them and the arguments
 * added with smdl_qc_compile_add_argument). "enabled" 0 removes the switch again. */
SMDL_API smdl_status smdl_qc_compile_set_no_warnings(smdl_qc_compile *compile, int enabled);          /* -nowarnings */
SMDL_API smdl_status smdl_qc_compile_set_max_warnings(smdl_qc_compile *compile, int enabled, int max); /* -maxwarnings */
/* -fitweights [poses]: fitting_poses / test_poses 1..4096, 0 for both = the default (24, 24) */
SMDL_API smdl_status smdl_qc_compile_set_fit_weights(smdl_qc_compile *compile, int enabled, int fitting_poses,
                                                     int test_poses);
SMDL_API smdl_status smdl_qc_compile_set_flex_optimize(smdl_qc_compile *compile, smdl_qc_flex_optimize mode);
SMDL_API smdl_status smdl_qc_compile_set_flex_optimize_min_static(smdl_qc_compile *compile, int enabled, int vertices);
SMDL_API smdl_status smdl_qc_compile_set_strip_lods(smdl_qc_compile *compile, int enabled);            /* -striplods */
SMDL_API smdl_status smdl_qc_compile_set_min_lod(smdl_qc_compile *compile, int enabled, int lod);      /* -minlod */
SMDL_API smdl_status smdl_qc_compile_set_define_bones(smdl_qc_compile *compile, int enabled);          /* -definebones */
SMDL_API smdl_status smdl_qc_compile_set_override_define_bones(smdl_qc_compile *compile, int enabled); /* -overridedefinebones */
SMDL_API smdl_status smdl_qc_compile_set_check_lengths(smdl_qc_compile *compile, int enabled);        /* -checklengths */
SMDL_API smdl_status smdl_qc_compile_set_print_bones(smdl_qc_compile *compile, int enabled);          /* -printbones */
SMDL_API smdl_status smdl_qc_compile_set_dump_materials(smdl_qc_compile *compile, int enabled);       /* -dumpmaterials */
SMDL_API smdl_status smdl_qc_compile_set_collapse_report(smdl_qc_compile *compile, int enabled);      /* -collapsereport */

/* Any other command line switch or switch value, one token per call, in order ("-t", "default", "-a", "5"). Checked by
 * the console tool's own parser: an unknown switch, a positional argument (the QC file is set by set_qc_path) and the
 * switches that need the console or the current directory (-makefile, -perf, -d, -mdlreport) fail with
 * SMDL_ERROR_INVALID_ARGUMENT and the parser's message in last_error; the argument is not added then. A switch may
 * wait for its value (the next call); a value that never comes is reported by the run. */
SMDL_API smdl_status smdl_qc_compile_add_argument(smdl_qc_compile *compile, const char *argument);

/* Host-provided files, consulted before the disk by every read of the compile (QC, $include, SMD/VTA/OBJ/VRD, DMX and
 * .mpp, mtllib, gameinfo.txt, ...). path: as the QC would reach it: absolute, or relative to the current directory.
 * Matching is on the normalized absolute path ('/' separators; exact first, then case-insensitively). The data is
 * copied. Adding the same path again replaces the file. */
SMDL_API smdl_status smdl_qc_compile_add_file(smdl_qc_compile *compile, const char *path, const void *data, size_t size);
/* Asked after the files above and before the disk; NULL removes it */
SMDL_API smdl_status smdl_qc_compile_set_reader(smdl_qc_compile *compile, smdl_qc_read_fn reader, void *user);

/* Receives every compiler message of the run (also kept in the result) */
SMDL_API smdl_status smdl_qc_compile_set_log(smdl_qc_compile *compile, smdl_log_fn log, void *user);

/* Also writes the outputs below <game_dir>/models/ (needs a game directory). Default 0: results in memory only. */
SMDL_API smdl_status smdl_qc_compile_set_write_to_game_dir(smdl_qc_compile *compile, int enabled);

/* Runs the compile; the handle can be run again (after changing options or files). *out_result is always set unless
 * out_result is NULL; destroy it with smdl_result_destroy. Returns SMDL_OK, SMDL_ERROR_COMPILE (details in the
 * result) or an argument / internal error (then last_error explains). */
SMDL_API smdl_status smdl_qc_compile_run(smdl_qc_compile *compile, smdl_result **out_result);

/* The whole console tool in a call: argv[0] is the program name, then studiomdl's switches, the QC file last (-game
 * <dir> is read as always). Returns the exit code studiomdl_v2 would. With a log function every message goes to it and
 * nothing is written to stdout (-perf timings are not printed then); without, the output is the console tool's.
 * Unlike the tool it does not change the working directory. Takes the same process-wide lock as every compile. */
SMDL_API int smdl_run_command_line(int argc, const char *const *argv, smdl_log_fn log, void *user);

#ifdef __cplusplus
}
#endif

#endif

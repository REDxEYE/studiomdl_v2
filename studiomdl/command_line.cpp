// The studiomdl command line switches: one parser for the console tool and for library hosts (command_line.hpp)
#include "studiomdl/command_line.hpp"

#include <string>

#include "studiomdl/apply_globals.hpp"
#include "studiomdl/compile.hpp"
#include "studiomdl/flexoptimize.h"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl/studiomdl_log.hpp"
#include "studiomdl/weight_fit.hpp"
#include "studiomdl_errors.h"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/string_utils.hpp"

void StudioMdlUsageError() {
    MdlError("Bad or missing options\n"
        "usage: studiomdl [options] <file.qc>\n"
        "options:\n"
        "[-a <normal_blend_angle>]\n"
        "[-checklengths]\n"
        "[-d] - dump glview files\n"
        "[-definebones]\n"
        "[-f] - flip all triangles\n"
        "[-fullcollide] - don't truncate really big collisionmodels\n"
        "[-game <gamedir>]\n"
        "[-h] - dump hboxes\n"
        "[-i] - ignore warnings\n"
        "[-minlod <lod>] - truncate to highest detail <lod>\n"
        "[-fitweights [<poses> | <fitting poses>,<test poses>]] - experimental: DMX vertices with more than 3\n"
        "                bones get weights fitted to their motion instead of the 3 largest rescaled.\n"
        "                Poses default to 24,24; one number sets both\n"
        "[-n] - tag bad normals\n"
        "[-perf] report perf info upon compiling model\n"
        "[-printbones]\n"
        "[-printgraph]\n"
        "[-quiet] - operate silently\n"
        "[-r] - tag reversed\n"
        "[-t <texture>]\n"
        "[-fastbuild] - write a single vertex windings file\n"
        "[-nowarnings] - disable warnings\n"
        "[-flexopt auto|on|off] - split flexed meshes to fit the engine flex cache (default auto)\n"
        "[-flexopt_min_static <verts>] - smallest static part split off a flexed mesh (default 256)\n"
        "[-dumpmaterials] - dump out material names\n"
        "[-mdlreport] model.mdl - report perf info\n"
        "[-mdlreportspreadsheet] - report perf info as a comma-delimited spreadsheet\n"
        "[-striplods] - use only lod0\n"
        "[-overridedefinebones] - equivalent to specifying $unlockdefinebones in " SRC_FILE_EXT " file\n"
        "[-allowdebug]\n"
        "[-ihvtest]\n"
        "[-overridedefinebones]\n"
        "[-verbose]\n"
        "[-makefile]\n"
        "[-verify]\n"
        "[-fastbuild]\n"
        "[-maxwarnings]\n"
        "[-preview]\n"
        "[-dumpmaterials]\n"
        "[-basedir]\n"
        "[-tempcontent]\n"
    );
}

void SetCompileInput(StudioMdlState &ctx, const std::string_view qcPath) {
    ctx.paths.qcPath = qcPath;
    ctx.paths.outname = vstd::paths::without_extension(qcPath);
}

// The value of -fitweights: "<poses>" sets both counts, "<fitting poses>,<test poses>" sets each
static bool ParseFitWeightsPoses(const std::string_view text, int &fitPoses, int &testPoses) {
    const auto parseCount = [](const std::string_view part, int &count) {
        if (part.empty() || part.size() > 4 || part.find_first_not_of("0123456789") != std::string_view::npos)
            return false;
        count = vstd::str_to_int<int>(part);
        return count >= 1 && count <= kMaxFitWeightsPoses;
    };

    int fit, test;
    if (const size_t comma = text.find(','); comma == std::string_view::npos) {
        if (!parseCount(text, fit))
            return false;
        test = fit;
    }
    else if (!parseCount(text.substr(0, comma), fit) || !parseCount(text.substr(comma + 1), test)) {
        return false;
    }
    fitPoses = fit;
    testPoses = test;
    return true;
}

ArgumentsResult ParseStudioMdlArguments(StudioMdlState &ctx, const std::span<const std::string_view> args,
                                        const ArgumentMode mode) {
    SetCompileDefaults(ctx);

    const bool library = mode == ArgumentMode::Options;
    const size_t argc = args.size();
    // Console tool: args[0] is the program and the last argument the QC file, which no option scan reaches (but a
    // switch that takes a value may swallow it: "-minlod x.qc" is then a malformed command line). Library: all
    // arguments are options.
    const size_t first = library ? 0 : 1;
    const size_t optionsEnd = library ? argc : (argc > 0 ? argc - 1 : 0);

    size_t i;
    for (i = first; i < optionsEnd; i++) {
        const std::string_view argValue = args[i];
        if (argValue.empty() || argValue[0] != '-') {
            if (library) {
                MdlError("unexpected argument \"{}\": the compile takes only switches (the QC file is set separately)\n",
                         argValue);
            }
            continue;
        }

        // The value of the switch at i; for the console tool the QC file is always there to be taken
        const auto nextValue = [&]() -> std::string_view {
            if (i + 1 >= argc) {
                MdlError("{} needs a value\n", argValue);
            }
            return args[++i];
        };
        const auto unsupportedInLibrary = [&] {
            if (library) {
                MdlError("{} is not supported here: it needs the console or the current directory\n", argValue);
            }
        };

        if (vstd::iequals(argValue, "-collapsereport")) {
            ctx.options.collapse_bones_message = true;
            continue;
        }

        if (vstd::iequals(argValue, "-parsecompletion")) {
            // reliably prints output we can parse for automatically
            ctx.options.parseable_completion_output = true;
            continue;
        }

        if (vstd::iequals(argValue, "-allowdebug")) {
            // Ignore, used by interface system to catch debug builds checked into release tree
            continue;
        }

        if (vstd::iequals(argValue, "-mdlreport")) {
            unsupportedInLibrary();
            // Will reparse later, ignore rest of arguments.
            return ArgumentsResult::MdlReport;
        }

        if (vstd::iequals(argValue, "-mdlreportspreadsheet")) {
            unsupportedInLibrary();
            // Will reparse later, ignore for now.
            continue;
        }

        if (vstd::iequals(argValue, "-overridedefinebones")) {
            ctx.options.defineBonesLockedByDefault = false;
            continue;
        }

        if (vstd::iequals(argValue, "-striplods")) {
            ctx.options.stripLods = true;
            continue;
        }

        if (vstd::iequals(argValue, "-quiet")) {
            ctx.options.quiet = true;
            ctx.options.verbose = false;
            continue;
        }

        if (vstd::iequals(argValue, "-verbose")) {
            ctx.options.quiet = false;
            ctx.options.verbose = true;
            continue;
        }


        if (vstd::iequals(argValue, "-checklengths")) {
            ctx.options.checkLengths = true;
            continue;
        }

        if (vstd::iequals(argValue, "-printbones")) {
            ctx.options.printBones = true;
            continue;
        }

        if (vstd::iequals(argValue, "-perf")) {
            // prints the timings to stdout after the compile
            unsupportedInLibrary();
            ctx.options.perf = true;
            continue;
        }

        if (vstd::iequals(argValue, "-fitweights")) {
            ctx.options.fitWeights = true;
            // optional pose counts (the last argument is the QC file, never a value)
            if (i + 1 < optionsEnd) {
                const std::string_view optCounts = args[i + 1];
                if (!optCounts.empty() && optCounts[0] >= '0' && optCounts[0] <= '9') {
                    if (!ParseFitWeightsPoses(optCounts, ctx.options.fitWeightsFitPoses,
                                              ctx.options.fitWeightsTestPoses)) {
                        MdlError("-fitweights: expected <poses> or <fitting poses>,<test poses> (1-{} each), got \"{}\"\n",
                                 kMaxFitWeightsPoses, optCounts);
                    }
                    i++;
                }
            }
            continue;
        }

        if (vstd::iequals(argValue, "-printgraph")) {
            ctx.options.dumpGraph = true;
            continue;
        }

        if (vstd::iequals(argValue, "-definebones")) {
            ctx.options.definebones = true;
            continue;
        }

        if (vstd::iequals(argValue, "-makefile")) {
            // appends to makefile.tmp in the current directory
            unsupportedInLibrary();
            ctx.options.createMakefile = true;
            ctx.options.quiet = true;
            ctx.paths.CreateMakefileCommand = args[0];
            continue;
        }

        if (vstd::iequals(argValue, "-verify")) {
            ctx.options.verifyOnly = true;
            continue;
        }

        if (vstd::iequals(argValue, "-minlod")) {
            ctx.options.minLod = vstd::str_to_int<int32_t>(nextValue());
            continue;
        }

        if (vstd::iequals(argValue, "-fastbuild")) {
            // Does nothing now, it's already faster that original studiomdl with -fastbuild
            // sw and dx80 are also strippped
            continue;
        }

        if (vstd::iequals(argValue, "-nowarnings")) {
            ctx.options.noWarnings = true;
            continue;
        }

        if (vstd::iequals(argValue, "-flexopt")) {
            const std::string_view pMode = nextValue();
            if (!ParseFlexOptimizeMode(pMode, ctx.options.flexOptimizeMode)) {
                MdlError("-flexopt expects auto, on or off, got \"{}\"\n", pMode);
            }
            continue;
        }

        if (vstd::iequals(argValue, "-flexopt_min_static")) {
            ctx.options.flexOptimizeMinStatic = vstd::str_to_int<int32_t>(nextValue());
            continue;
        }

        if (vstd::iequals(argValue, "-maxwarnings")) {
            ctx.options.maxWarnings = vstd::str_to_int<int32_t>(nextValue());
            continue;
        }

        if (vstd::iequals(argValue, "-preview")) {
            ctx.options.buildPreview = true;
            continue;
        }

        if (vstd::iequals(argValue, "-dumpmaterials")) {
            ctx.options.dumpMaterials = true;
            continue;
        }

        // single letter switches (-t -a -h -i -d); a string_view is not NUL terminated, so test the length
        bool handled = false;
        if (argValue.size() == 2) {
            handled = true;
            switch (argValue[1]) {
                case 't': {
                    // -t <default texture> [<texture it replaces>]; without the second, every texture is replaced
                    const std::string_view replacement = nextValue();
                    std::string_view replaces;
                    if (i + 1 < optionsEnd && (args[i + 1].empty() || args[i + 1][0] != '-')) {
                        replaces = args[++i];
                        MdlPrint("Replacing {} with {}\n", replaces, replacement);
                    }
                    ApplyTextureReplacement(ctx, replacement, replaces);
                    MdlPrint("Using default texture: {}\n", ctx.materials.defaulttextures[0]);
                    break;
                }
                case 'a':
                    ApplyNormalBlendAngle(ctx, vstd::str_to_float<float>(nextValue()));
                    break;
                case 'h':
                    ctx.options.dump_hboxes = 1;
                    break;
                case 'i':
                    ctx.options.ignore_warnings = 1;
                    break;
                case 'd':
                    // writes .glview files into the current directory
                    unsupportedInLibrary();
                    ctx.write.dumpGLViewFiles = true;
                    break;
                    //			case 'p':
                    //				i++;
                    //				strcpy( qproject, pArgv );
                    //				break;
                default:
                    handled = false;
                    break;
            }
        }

        if (!handled && library) {
            MdlError("unknown option \"{}\"\n", argValue);
        }
    }

    if (library)
        return ArgumentsResult::Compile;

    if (i >= argc) {
        // misformed arguments
        // otherwise generating unintended results
        StudioMdlUsageError();
    }

    SetCompileInput(ctx, args[i]);
    return ArgumentsResult::Compile;
}

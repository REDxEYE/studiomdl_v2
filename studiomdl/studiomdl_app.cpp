// The studiomdl application: command line, logging and process setup around one compile (compile.cpp)
#include "studiomdl_app.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <new>
#include <print>
#include <vector>

#include "studiomdl/platform_compat.h"

#include "common/cmdlib.h"
#include "filesystem.h"
#include "datamodel/idatamodel.h"
#include "interfaces/interfaces.h"
#include "mathlib/mathlib.h"
#include "studiomdl/apply_globals.hpp"
#include "studiomdl/compile.hpp"
#include "studiomdl/command_line.hpp"
#include "studiomdl/flexoptimize.h"
#include "studiomdl/perfstats.h"
#include "studiomdl/processing.hpp"
#include "studiomdl/studiomdl.h"
#include "studiomdl/studiomdl_global_state.hpp"
#include "studiomdl/studiomdl_log.hpp"
#include "studiomdl_errors.h"
#include "tier0/icommandline.h"
#include "tier1_v2/function_timing.hpp"
#include "tier1_v2/math_utils.hpp"
#include "tier1_v2/path_utils.hpp"
#include "tier1_v2/string_utils.hpp"

CStudioMDLApp::CStudioMDLApp() = default;

CStudioMDLApp::CStudioMDLApp(bool library, std::function<void(MdlSeverity, std::string_view)> sink)
    : m_Library(library), m_Sink(std::move(sink)) {}

CStudioMDLApp::~CStudioMDLApp() = default;

CMdlLoggingListener s_MdlLoggingListener;

int RunStudioMdl(int argc, char** argv) {
    CStudioMDLApp app;
    return app.Run(argc, argv);
}

int RunStudioMdlLibrary(int argc, char** argv, std::function<void(MdlSeverity, std::string_view)> sink) {
    CStudioMDLApp app(true, std::move(sink));
    return app.Run(argc, argv);
}

// -PrintSurfaceProps <model.mdl> (CheckSurfaceProps.py): prints the surface props of the model's bones instead of
// compiling
static bool HandlePrintSurfaceProps(int &returnValue) {
    const auto filename = CommandLine()->ParmValue("-PrintSurfaceProps");
    if (!filename)
        return false;
    returnValue = PrintModelSurfaceProps(*filename);
    return true;
}

// -mdlreport <model.mdl> [-mdlreportspreadsheet] (modelstats.pl): prints the model's perf info instead of compiling
static bool HandleMdlReport(StudioMdlState &ctx, int &returnValue) {
    const auto filename = CommandLine()->ParmValue("-mdlreport");
    if (!filename)
        return false;
    returnValue = ReportModelPerf(ctx, *filename, CommandLine()->FindParm("-mdlreportspreadsheet").has_value());
    return true;
}
//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
int CStudioMDLApp::Main() {
    StudioMdlState &ctx = *m_pState;
    int nReturnValue;
    {
        vstd::FunctionTimingRecorder _("main");
        // This bit of hackery allows us to access files on the harddrive
        g_pFullFileSystem->AddSearchPath("", IBaseFileSystem::PathID::Local, PATH_ADD_TO_HEAD);

        if (HandleMdlReport(ctx, nReturnValue))
            return false;

        nReturnValue = CompileModel(ctx);
    }

    // The timings go to stdout: a host that collects the messages doesn't get them
    if (ctx.options.perf && !ctx.diagnostics.sink) {
        vstd::print_function_timings();
    }

    return nReturnValue;
}

// A failed compile (or setup) unwinds to here (CApplication::Run). The console tool reproduces what ending the process
// in MdlError / Error used to do: same output, same exit code.
int CStudioMDLApp::OnException(const std::exception_ptr &error) {
    StudioMdlState *pCtx = m_pState.get();
    int exitCode;
    try {
        std::rethrow_exception(error);
    }
    catch (const MdlCompileError &) {
        // MdlError printed the message; the aftermath is the old exit(-1) path's. Elements left without a file now
        // are real leaks (scratch elements are RAII-owned), so they are still reported.
        if (pCtx)
            MdlAbortCompile(*pCtx);
        if (const int leaked = MdlDiscardDataModelContents(); leaked > 0)
            Warning("Leaking {:d} elements\n", leaked);
        exitCode = 255;
    }
    catch (const Plat_ExitException &e) {
        // tier0 Error / LR_ABORT: the message is logged; the process used to end right there with this code
        MdlDiscardDataModelContents();
        exitCode = e.exitCode();
    }
    catch (const std::bad_alloc &) {
        if (m_Library)
            MdlPrint("ERROR: Out of memory\n");
        else
            std::fputs("Out of memory", stderr);
        MdlDiscardDataModelContents();
        exitCode = 255;
    }
    catch (const std::exception &e) {
        MdlPrint("ERROR: {}\n", e.what());
        if (pCtx)
            MdlAbortCompile(*pCtx);
        MdlDiscardDataModelContents();
        exitCode = 255;
    }

    EndProcessing();
    return exitCode;
}

//-----------------------------------------------------------------------------
// The application object
//-----------------------------------------------------------------------------
bool CStudioMDLApp::Create() {
    // The compile state and its diagnostics scope live until Destroy()
    m_pState = std::make_unique<StudioMdlState>();
    m_pDiagnostics = std::make_unique<ScopedMdlDiagnostics>(*m_pState);
    m_pState->diagnostics.sink = m_Sink;

    // A library host keeps its process as it is: no stdout buffering change (InstallSpewFunction) and no crash handler
    if (!m_Library) {
        // Ensure that cmdlib spew function & associated state is initialized
        InstallSpewFunction();
    }
    // Override the cmdlib spew function
    LoggingSystem_PushLoggingState();
    LoggingSystem_RegisterLoggingListener(&s_MdlLoggingListener);

    MathLib_Init(2.2f, 2.2f, 0.0f, 2.0f, false, true, true, true);

    if (!m_Library)
        SetUnhandledExceptionFilter(VExceptionFilter);

    // Clean process-wide state (datamodel, search paths) before this compile sets anything up
    if (!BeginProcessing())
        return false;

    if (CommandLine()->ParmCount() == 1) {
        StudioMdlUsageError();
        return false;
    }

    int nReturnValue;
    if (HandlePrintSurfaceProps(nReturnValue))
        return false;

    if (!ParseArguments(*m_pState))
        return false;


    return true;
}

void CStudioMDLApp::Shutdown() {
    EndProcessing();
}

void CStudioMDLApp::Destroy() {
    // No-op after Shutdown; ends the processing when Create/PreInit failed after BeginProcessing
    EndProcessing();
    LoggingSystem_PopLoggingState();
    m_pDiagnostics.reset();
    m_pState.reset();
}

bool CStudioMDLApp::PreInit() {
    StudioMdlState &ctx = *m_pState;
    g_pFileSystem = g_pFullFileSystem;

    if (!SetupSearchPaths())
        return false;

    SetupCompileDirectories(ctx, ctx.paths.qcPath, GetGameInfoPath());
    return true;
}

//-----------------------------------------------------------------------------
// Method which parses arguments
//-----------------------------------------------------------------------------
bool CStudioMDLApp::ParseArguments(StudioMdlState &ctx) {
    // the command line as the shared parser takes it (UTF-8): the program, the options, the QC file last
    std::vector<std::string_view> args;
    const uint32_t argc = CommandLine()->ParmCount();
    args.reserve(argc);
    for (uint32_t i = 0; i < argc; i++) {
        args.push_back(CommandLine()->GetParm(i).value_or(std::string_view()));
    }

    if (ParseStudioMdlArguments(ctx, args, ArgumentMode::CommandLine) == ArgumentsResult::MdlReport)
        return true;

    if (!m_Library && vstd::paths::is_absolute(ctx.paths.qcPath)) {
        // Set the working directory to be the path of the qc file
        // so the relative-file fopen code works
        std::error_code ec;
        std::filesystem::current_path(vstd::paths::to_fs(vstd::paths::directory(ctx.paths.qcPath)), ec);
    }
    return true;
}

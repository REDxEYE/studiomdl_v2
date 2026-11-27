# Native Qt port of the Half-Life Model Viewer; upstream/ holds the original Win32 sources for reference only.
# render/: render system (viewport.cpp), SDK filesystem glue, VTF upload, game-agnostic material system and shader
# interface (shader_api.h), and the SFM stdshader_dx9 port (sfm_shaders.cpp); another game's shaders would be a
# sibling of sfm_shaders.cpp. Search paths come from gameinfo.txt through studiomdl's filesystem_init, as dmxconvert does.
add_library(hlmv_render STATIC
        render/viewport.cpp render/mdl_mesh.cpp render/mdl_flex.cpp render/studio_anim.cpp render/game_filesystem.cpp
        render/texture_upload.cpp render/material_system.cpp render/sfm_shaders.cpp ${PROJECT_SOURCE_DIR}/studiomdl/filesystem_init.cpp)
target_include_directories(hlmv_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/render PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(hlmv_render PUBLIC STATIC_TIER0) # consumers include SDK headers through mdl_mesh.h
# bonesetup/utils/mathlib: the engine's animation code (IBoneSetup, CIKContext, CStudioHdr) used by studio_anim.cpp
target_link_libraries(hlmv_render PUBLIC Qt6::OpenGL Qt6::OpenGLWidgets
        PRIVATE bonesetup utils mathlib filesystem_stdio_v2_static tier2 tier1 tier0 togl_translator)
if (MINGW)
    target_link_libraries(hlmv_render PRIVATE stdc++exp)
endif ()

if (STUDIOMDL_BUILD_HLMV)
    add_executable(hlmv main.cpp mdlviewer.cpp controlpanel.cpp mx.cpp)
    target_link_libraries(hlmv PRIVATE hlmv_render Qt6::Widgets)
    # Upstream HLMV's MX_ICON, as the window/taskbar icon (X11 _NET_WM_ICON, Wayland xdg-toplevel-icon)
    qt_add_resources(hlmv "icon" PREFIX "/hlmv" BASE upstream FILES upstream/icon1.ico)
endif ()

# Standalone shader probe: renders a quad or MDL with supplied GLSL and self-checks the output
if (STUDIOMDL_BUILD_SFM_GL_PROBE)
    add_executable(sfm_gl_probe render/probe_main.cpp)
    target_link_libraries(sfm_gl_probe PRIVATE hlmv_render)
endif ()

# Shared Qt/OpenGL renderer for SFM models (used by hlmv) and the standalone shader probe
add_library(sfm_viewport STATIC viewport.cpp mdl_mesh.cpp source_texture.cpp)
target_include_directories(sfm_viewport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(sfm_viewport PRIVATE STATIC_TIER0)
target_link_libraries(sfm_viewport PUBLIC Qt6::OpenGL PRIVATE tier1 togl_translator)
if (MINGW)
    target_link_libraries(sfm_viewport PRIVATE stdc++exp)
endif ()

if (STUDIOMDL_BUILD_SFM_GL_PROBE)
    add_executable(sfm_gl_probe main.cpp)
    target_link_libraries(sfm_gl_probe PRIVATE sfm_viewport)
endif ()

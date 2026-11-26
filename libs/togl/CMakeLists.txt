# The D3D9 shader bytecode to GLSL translator from the CS:GO togl (the GL runtime itself is not ported)
add_library(togl_translator STATIC
        dx9asmtogl2.cpp
        vcs.cpp
        ../../include/togl/dx9asmtogl2.h
        ../../include/togl/d3d9_shader_tokens.h
)
target_include_directories(togl_translator PUBLIC ../../include)
target_link_libraries(togl_translator PUBLIC tier0 tier1 PRIVATE LibLZMA::LibLZMA)

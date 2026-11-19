# Extracts the real ClampMaxVerticesPerModel / CClampedSource code from the compiler sources so the test compiles it
# against the shims in shim.h.
#   cmake -DSRC_DIR=<repo>/studiomdl -DHDR=<repo>/include/studiomdl/studiomdl.h -DOUT_DIR=<dir> -P extract.cmake

# Writes the text of FILE from the line starting with BEGIN up to (not including) the line starting with END,
# or up to and including the first "};" line when END is "};"
function(extract FILE BEGIN END OUT)
    file(READ "${FILE}" text)
    string(REPLACE "\r\n" "\n" text "${text}")
    string(FIND "${text}" "\n${BEGIN}" start)
    if (start EQUAL -1)
        message(FATAL_ERROR "extract.cmake: \"${BEGIN}\" not found in ${FILE}")
    endif ()
    math(EXPR start "${start} + 1")
    string(SUBSTRING "${text}" ${start} -1 rest)
    string(FIND "${rest}" "\n${END}" stop)
    if (stop EQUAL -1)
        message(FATAL_ERROR "extract.cmake: \"${END}\" not found after \"${BEGIN}\" in ${FILE}")
    endif ()
    if (END STREQUAL "};")
        math(EXPR stop "${stop} + 3")
    endif ()
    string(SUBSTRING "${rest}" 0 ${stop} body)
    file(WRITE "${OUT}" "${body}\n")
endfunction()

extract("${HDR}" "class CClampedSource {" "};" "${OUT_DIR}/class_decl.inc")
# AddNewVert .. Init are contiguous; the "//----" comment block of ClampMaxVerticesPerModel follows them, and the one of
# PostProcessSource follows ClampMaxVerticesPerModel
extract("${SRC_DIR}/studiomdl.cpp" "int CClampedSource::AddNewVert" "//----" "${OUT_DIR}/clamped_impl.inc")
extract("${SRC_DIR}/studiomdl.cpp" "void ClampMaxVerticesPerModel" "//----" "${OUT_DIR}/clamp_fn.inc")

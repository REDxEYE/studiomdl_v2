# ctest api_exports: the dynamic symbol table of libstudiomdl.so defines smdl_* functions and nothing else.
# usage: cmake -DNM=<nm> -DLIBRARY=<libstudiomdl.so> -P check_exports.cmake
execute_process(COMMAND ${NM} -D --defined-only ${LIBRARY}
        OUTPUT_VARIABLE symbols ERROR_VARIABLE errors RESULT_VARIABLE result)
if (NOT result EQUAL 0)
    message(FATAL_ERROR "${NM} -D failed on ${LIBRARY}: ${errors}")
endif ()

string(REPLACE "\n" ";" lines "${symbols}")
set(exported 0)
set(foreign "")
foreach (line IN LISTS lines)
    if (line STREQUAL "")
        continue()
    endif ()
    # "<address> <type> <name>"
    string(REGEX REPLACE "^[0-9a-fA-F]* +[A-Za-z] +" "" name "${line}")
    if (name MATCHES "^smdl_")
        math(EXPR exported "${exported} + 1")
    else ()
        list(APPEND foreign "${name}")
    endif ()
endforeach ()

if (exported EQUAL 0)
    message(FATAL_ERROR "${LIBRARY} exports no smdl_* symbol")
endif ()
if (foreign)
    list(LENGTH foreign foreign_count)
    list(JOIN foreign "\n  " foreign_text)
    message(FATAL_ERROR "${LIBRARY} exports ${foreign_count} symbol(s) that are not smdl_*:\n  ${foreign_text}")
endif ()
message(STATUS "${LIBRARY}: ${exported} exported symbols, all smdl_*")

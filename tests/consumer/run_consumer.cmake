# ctest api_consumer: configure and build tests/consumer (a separate project that add_subdirectory()s this repository
# with studiomdl::static), then run it against the output of the shared-library C host.
# usage: cmake -DSOURCE_DIR=<tests/consumer> -DBINARY_DIR=<scratch build dir> -DC_HOST=<api_c_host> -DGENERATOR=<name>
#              -DBUILD_TYPE=<type> -DCXX_COMPILER=<path> -DC_COMPILER=<path> [-DNM=<nm>] [-DEXE_SUFFIX=.exe]
#              [-DOSX_ARCHITECTURES=<arch>] [-DOSX_DEPLOYMENT_TARGET=<version>] -P run_consumer.cmake
function(run description)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "${description} failed (${result}): ${ARGN}")
    endif ()
endfunction()

file(REMOVE_RECURSE ${BINARY_DIR}/out)
set(osx_args "")
if (OSX_ARCHITECTURES)
    list(APPEND osx_args -DCMAKE_OSX_ARCHITECTURES=${OSX_ARCHITECTURES})
endif ()
if (OSX_DEPLOYMENT_TARGET)
    list(APPEND osx_args -DCMAKE_OSX_DEPLOYMENT_TARGET=${OSX_DEPLOYMENT_TARGET})
endif ()
run("configure" ${CMAKE_COMMAND} -S ${SOURCE_DIR} -B ${BINARY_DIR} -G ${GENERATOR}
        -DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CXX_COMPILER} -DCMAKE_C_COMPILER=${C_COMPILER}
        ${osx_args})
run("build" ${CMAKE_COMMAND} --build ${BINARY_DIR} --target consumer_host)

# The host must not have pulled in anything the subproject mode excludes
foreach (target studiomdl_v2 dmxconvert smdl_api_tests studiomdl_shared)
    if (EXISTS ${BINARY_DIR}/${target}${EXE_SUFFIX} OR EXISTS ${BINARY_DIR}/studiomdl/${target}${EXE_SUFFIX})
        message(FATAL_ERROR "subproject mode built ${target}")
    endif ()
endforeach ()

# Element factories (self-registering objects) must be in the executable although nothing references them
if (NM)
    execute_process(COMMAND ${NM} ${BINARY_DIR}/consumer_host${EXE_SUFFIX} OUTPUT_VARIABLE symbols RESULT_VARIABLE result)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "${NM} failed on consumer_host")
    endif ()
    foreach (symbol g_CDmeHitbox_TypeInfo g_CDmeBodyPart_TypeInfo g_CDmeCollisionModel_TypeInfo)
        # Mach-O prefixes C++ and C symbols with an underscore
        if (NOT symbols MATCHES " _?${symbol}\n")
            message(FATAL_ERROR "consumer_host lacks ${symbol}: the element factories were dropped from the static link")
        endif ()
    endforeach ()
endif ()

file(REMOVE_RECURSE ${BINARY_DIR}/out)
run("C host" ${C_HOST} ${BINARY_DIR}/out/shared)
run("consumer host" ${BINARY_DIR}/consumer_host${EXE_SUFFIX} ${BINARY_DIR}/out/static ${BINARY_DIR}/out/shared)

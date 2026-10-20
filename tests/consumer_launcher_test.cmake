# Exercise the installed-consumer harness and the same generated-command runner
# with a small consumer. The separate install_consumer check still builds and
# runs the complete installed public-header consumer.
include("${CMAKE_CURRENT_LIST_DIR}/consumer_launcher_path_test.cmake")
file(REMOVE_RECURSE "${ARKNET_LAUNCHER_TEST_DIR}")
file(MAKE_DIRECTORY "${ARKNET_LAUNCHER_TEST_DIR}/fixture/tests/consumer")
set(fixture "${ARKNET_LAUNCHER_TEST_DIR}/fixture")
set(parent "${ARKNET_LAUNCHER_TEST_DIR}/parent")
file(WRITE "${fixture}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.20)\nproject(launcher_fixture LANGUAGES CXX)\n"
    "add_executable(recorder [==[${ARKNET_SOURCE_DIR}/tests/consumer_launcher.cpp]==])\n"
    "target_compile_features(recorder PRIVATE cxx_std_20)\n"
    "set_target_properties(recorder PROPERTIES RUNTIME_OUTPUT_DIRECTORY [==[${ARKNET_LAUNCHER_TEST_DIR}/launcher with spaces]==])\n"
    "file(GENERATE OUTPUT [==[${parent}/recorder-$<CONFIG>.txt]==] CONTENT \"$<TARGET_FILE:recorder>\")\n"
    "install(FILES CMakeLists.txt DESTINATION fixture)\n")
file(WRITE "${fixture}/tests/consumer/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.20)
project(consumer_fixture LANGUAGES CXX)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/expected-launcher.txt" expected)
if(NOT "${CMAKE_CXX_COMPILER_LAUNCHER}" STREQUAL "${expected}")
    message(FATAL_ERROR "Consumer launcher differs: '${CMAKE_CXX_COMPILER_LAUNCHER}' != '${expected}'")
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(consumer main.cpp)
add_custom_target(check_consumer COMMAND consumer DEPENDS consumer)
]=])
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${fixture}" -B "${parent}" -G "${ARKNET_GENERATOR}"
    "-DCMAKE_CXX_COMPILER=${ARKNET_CXX_COMPILER}" "-DCMAKE_BUILD_TYPE=${ARKNET_CONFIG}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Launcher fixture configure failed\n${output}${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${parent}" --config "${ARKNET_CONFIG}" --parallel 2
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 60)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Launcher fixture build failed\n${output}${error}")
endif()
file(READ "${parent}/recorder-${ARKNET_CONFIG}.txt" recorder)
foreach(case empty spaces arguments failure)
    file(REMOVE_RECURSE "${parent}/consumer-smoke")
    set(launcher "${recorder}")
    if(case STREQUAL "empty")
        set(launcher "")
    elseif(case STREQUAL "arguments" OR case STREQUAL "failure")
        set(launcher "${recorder};--record-token;first value;--record-token;second")
    endif()
    set(evidence "${ARKNET_LAUNCHER_TEST_DIR}/${case}")
    file(MAKE_DIRECTORY "${evidence}")
    file(WRITE "${fixture}/tests/consumer/expected-launcher.txt" "${launcher}")
    file(WRITE "${fixture}/tests/consumer/main.cpp" "int main() { return 0; }\n")
    set(ENV{ARKNET_TEST_LAUNCHER_LOG} "${evidence}/invocations.log")
    if(case STREQUAL "failure")
        file(WRITE "${fixture}/tests/consumer/main.cpp" "#error intentional consumer compiler failure\n")
    endif()
    # arknet_add_check emits bracket-quoted arguments; run.cmake escapes list
    # separators before execute_process. Keep that complete boundary in this test.
    file(WRITE "${evidence}/commands.cmake"
        "arknet_run_check(consumer 60 [==[${CMAKE_COMMAND}]==]"
        " [==[-DARKNET_SOURCE_DIR=${fixture}]==] [==[-DARKNET_BUILD_DIR=${parent}]==]"
        " [==[-DARKNET_CXX_COMPILER=${ARKNET_CXX_COMPILER}]==]"
        " [==[-DARKNET_CXX_COMPILER_LAUNCHER=${launcher}]==]"
        " [==[-DARKNET_CONFIG=${ARKNET_CONFIG}]==] [==[-DARKNET_GENERATOR=${ARKNET_GENERATOR}]==]"
        " -P [==[${ARKNET_SOURCE_DIR}/tests/install_consumer.cmake]==])\n")
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DARKNET_TEST_COMMANDS=${evidence}/commands.cmake" "-DARKNET_TEST_LOG_DIR=${evidence}/logs"
        -P "${ARKNET_SOURCE_DIR}/tests/run.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 70)
    file(READ "${evidence}/logs/consumer.log" consumer_log)
    if(case STREQUAL "failure")
        if(result STREQUAL "0" OR NOT consumer_log MATCHES "intentional consumer compiler failure" OR
            NOT consumer_log MATCHES "Installed consumer build failed")
            message(FATAL_ERROR "Consumer hid compiler failure\n${consumer_log}\n${output}${error}")
        endif()
    elseif(NOT result STREQUAL "0" OR NOT consumer_log MATCHES "Installed consumer: run")
        message(FATAL_ERROR "Consumer ${case} did not build and run\n${consumer_log}\n${output}${error}")
    endif()
    file(READ "${parent}/consumer-smoke/CMakeCache.txt" cache)
    string(REGEX MATCH "CMAKE_CXX_COMPILER_LAUNCHER:[^=\n]*=([^\n]*)" cache_entry "${cache}")
    if((cache_entry AND NOT "${CMAKE_MATCH_1}" STREQUAL "${launcher}") OR
        (NOT cache_entry AND NOT "${launcher}" STREQUAL ""))
        message(FATAL_ERROR "Consumer ${case} cache lost launcher: ${cache_entry}")
    endif()
    file(COPY "${parent}/consumer-smoke/CMakeCache.txt" DESTINATION "${evidence}")
    if(EXISTS "${parent}/consumer-smoke/compile_commands.json")
        file(COPY "${parent}/consumer-smoke/compile_commands.json" DESTINATION "${evidence}")
    endif()
    if(case STREQUAL "empty")
        if(EXISTS "${evidence}/invocations.log")
            message(FATAL_ERROR "Empty launcher invoked recorder")
        endif()
    else()
        file(READ "${evidence}/invocations.log" invocations)
        arknet_consumer_compiler_recorded(compiler_matches "${ARKNET_CXX_COMPILER}" "${invocations}")
        if(NOT compiler_matches)
            message(FATAL_ERROR "Consumer did not invoke the requested compiler\n${invocations}")
        endif()
        if(case STREQUAL "arguments" OR case STREQUAL "failure")
            string(REPLACE "\r\n" "\n" invocations "${invocations}")
            string(FIND "${invocations}" "token=first value\ntoken=second\n" token_position)
            if(token_position EQUAL -1)
                message(FATAL_ERROR "Consumer split launcher arguments\n${invocations}")
            endif()
        endif()
    endif()
    message(STATUS "Consumer launcher ${case}: passed")
endforeach()

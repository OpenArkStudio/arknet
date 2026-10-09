file(MAKE_DIRECTORY "${ARKNET_RUNNER_TEST_DIR}/with spaces")
set(manifest "${ARKNET_RUNNER_TEST_DIR}/with spaces/commands.cmake")
set(logs "${ARKNET_RUNNER_TEST_DIR}/logs")
file(REMOVE "${logs}/pass.log" "${logs}/arguments.log" "${logs}/failure.log"
    "${logs}/timeout.log" "${logs}/after_failure.log")
file(WRITE "${ARKNET_RUNNER_TEST_DIR}/arguments.cmake"
    "if(NOT VALUE STREQUAL \"first;second\")\n"
    "  message(FATAL_ERROR \"Command argument was split\")\n"
    "endif()\n")
file(WRITE "${manifest}"
    "arknet_run_check(pass 5 [==[${CMAKE_COMMAND}]==] -E true)\n"
    "arknet_run_check(arguments 5 [==[${CMAKE_COMMAND}]==] [==[-DVALUE=first;second]==] -P [==[${ARKNET_RUNNER_TEST_DIR}/arguments.cmake]==])\n"
    "arknet_run_check(failure 5 [==[${CMAKE_COMMAND}]==] -E false)\n"
    "arknet_run_check(timeout 1 [==[${CMAKE_COMMAND}]==] -E sleep 5)\n"
    "arknet_run_check(after_failure 5 [==[${CMAKE_COMMAND}]==] -E true)\n")
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DARKNET_TEST_COMMANDS=${manifest}" "-DARKNET_TEST_LOG_DIR=${logs}"
    -P "${CMAKE_CURRENT_LIST_DIR}/run.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
if(result STREQUAL "0" OR NOT EXISTS "${logs}/after_failure.log")
    message(FATAL_ERROR "Runner hid a failure or did not continue\n${output}${error}")
endif()
foreach(name pass arguments after_failure)
    file(READ "${logs}/${name}.log" passed_log)
    if(NOT passed_log MATCHES "result: 0")
        message(FATAL_ERROR "Runner did not complete ${name}\n${passed_log}")
    endif()
endforeach()
file(READ "${logs}/failure.log" failure_log)
if(NOT failure_log MATCHES "result: 1")
    message(FATAL_ERROR "Runner did not retain the failed command result\n${failure_log}")
endif()
file(READ "${logs}/timeout.log" timeout_log)
if(NOT timeout_log MATCHES "result: .*timeout")
    message(FATAL_ERROR "Runner did not enforce its timeout\n${timeout_log}")
endif()

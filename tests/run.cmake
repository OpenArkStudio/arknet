file(MAKE_DIRECTORY "${ARKNET_TEST_LOG_DIR}")

function(arknet_run_check name timeout)
    message(STATUS "Running ${name}")
    set(command)
    math(EXPR last "${ARGC} - 1")
    foreach(index RANGE 2 ${last})
        string(REPLACE ";" "\\;" argument "${ARGV${index}}")
        list(APPEND command "${argument}")
    endforeach()
    execute_process(COMMAND ${command} TIMEOUT ${timeout}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(WRITE "${ARKNET_TEST_LOG_DIR}/${name}.log"
        "command: ${command}\nresult: ${result}\n${output}${error}")
    if(NOT result STREQUAL "0")
        message(SEND_ERROR "${name} failed (${result})\n${output}${error}")
    elseif(NOT "${output}${error}" STREQUAL "")
        message("${output}${error}")
    endif()
endfunction()

include("${ARKNET_TEST_COMMANDS}")

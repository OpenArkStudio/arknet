execute_process(COMMAND "${PROGRAM}" RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
if(result STREQUAL "0" OR NOT error MATCHES "Windows exception code=0xe0424e42")
    message(FATAL_ERROR "Crash reporting did not preserve a fatal exit and diagnostic\n${result}\n${output}${error}")
endif()
message(STATUS "Windows crash reporter preserves a fatal exit and records the exception")

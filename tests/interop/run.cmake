if(NOT EXISTS "${ARKNET_CURRENT_PEER}" OR NOT EXISTS "${ARKNET_LEGACY_PEER}")
    message(FATAL_ERROR "Build both RUDP interoperability peer executables first")
endif()

foreach(server_provider current legacy)
    if(server_provider STREQUAL "current")
        set(server "${ARKNET_CURRENT_PEER}")
        set(client "${ARKNET_LEGACY_PEER}")
        set(pair "zephyr client -> arknet server")
    else()
        set(server "${ARKNET_LEGACY_PEER}")
        set(client "${ARKNET_CURRENT_PEER}")
        set(pair "arknet client -> zephyr server")
    endif()
    foreach(scenario baseline reconnect impaired)
      foreach(closer client server)
        message(STATUS "Checking ${pair}, ${scenario}, close initiated by ${closer}")
        # The server publishes its ephemeral port through the pipe before the client connects.
        execute_process(
            COMMAND "${server}" server "${closer}" "${scenario}"
            COMMAND "${client}" client "${closer}" "${scenario}"
            TIMEOUT 40
            RESULTS_VARIABLE results OUTPUT_VARIABLE output ERROR_VARIABLE error)
        foreach(result IN LISTS results)
            if(NOT result STREQUAL "0")
                message(FATAL_ERROR "${pair}, ${scenario}, ${closer} close failed (${results})\n${output}${error}")
            endif()
        endforeach()
        message("${output}${error}")
      endforeach()
    endforeach()
endforeach()

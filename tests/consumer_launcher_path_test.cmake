function(arknet_consumer_compiler_recorded result compiler invocations)
    # CMake and Windows argv can spell the same path with different separators.
    # Match a complete compiler record after normalization, not a path substring.
    string(REPLACE "\\" "/" expected "${compiler}")
    string(REPLACE "\\" "/" actual "${invocations}")
    string(REPLACE "\r\n" "\n" actual "${actual}")
    string(FIND "\n${actual}" "\ncompiler=${expected}\n" position)
    if(position EQUAL -1)
        set(${result} FALSE PARENT_SCOPE)
    else()
        set(${result} TRUE PARENT_SCOPE)
    endif()
endfunction()

set(forward "C:/Program Files/Visual Studio/bin/cl.exe")
string(REPLACE "/" "\\" backward "${forward}")
foreach(requested IN ITEMS "${forward}" "${backward}")
    foreach(recorded IN ITEMS "${forward}" "${backward}")
        arknet_consumer_compiler_recorded(matches "${requested}" "compiler=${recorded}\r\n")
        if(NOT matches)
            message(FATAL_ERROR "Equivalent compiler paths did not match: ${requested}, ${recorded}")
        endif()
    endforeach()
endforeach()
foreach(record IN ITEMS
    "compiler=C:/Program Files/Visual Studio/bin/clang-cl.exe\n"
    "compiler=D:/other/bin/cl.exe\n"
    "compiler=${forward}.other\n"
    "argument=compiler=${forward}\n")
    arknet_consumer_compiler_recorded(matches "${forward}" "${record}")
    if(matches)
        message(FATAL_ERROR "Unrelated compiler record matched: ${record}")
    endif()
endforeach()
message(STATUS "Consumer compiler path records: passed")

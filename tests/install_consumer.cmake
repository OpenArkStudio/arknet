function(run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${ARGV}\n${output}\n${error}")
    endif()
endfunction()
set(prefix "${ARKNET_BUILD_DIR}/install-smoke")
run("${CMAKE_COMMAND}" --install "${ARKNET_BUILD_DIR}" --prefix "${prefix}" --config "${ARKNET_CONFIG}")
set(vcpkg_args)
if(ARKNET_VCPKG_INSTALLED_DIR)
    list(APPEND vcpkg_args "-DVCPKG_INSTALLED_DIR=${ARKNET_VCPKG_INSTALLED_DIR}")
endif()
if(ARKNET_VCPKG_TRIPLET)
    list(APPEND vcpkg_args "-DVCPKG_TARGET_TRIPLET=${ARKNET_VCPKG_TRIPLET}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${ARKNET_SOURCE_DIR}/tests/consumer" -B "${ARKNET_BUILD_DIR}/consumer-smoke"
    -G "${ARKNET_GENERATOR}"
    "-DCMAKE_CXX_COMPILER=${ARKNET_CXX_COMPILER}"
    "-DCMAKE_TOOLCHAIN_FILE=${ARKNET_TOOLCHAIN}"
    "-DCMAKE_BUILD_TYPE=${ARKNET_CONFIG}"
    "-DCMAKE_PREFIX_PATH=${prefix};${ARKNET_DEPENDENCY_PREFIX}"
    "-DARKNET_ASIO_INCLUDE_DIR=${ARKNET_ASIO_INCLUDE_DIR}"
    "-DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}"
    -DVCPKG_MANIFEST_INSTALL=OFF
    ${vcpkg_args}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "${output}\n${error}")
endif()
run("${CMAKE_COMMAND}" --build "${ARKNET_BUILD_DIR}/consumer-smoke" --config "${ARKNET_CONFIG}" --parallel 2)
run("${CMAKE_COMMAND}" --build "${ARKNET_BUILD_DIR}/consumer-smoke"
    --config "${ARKNET_CONFIG}" --target check_consumer)

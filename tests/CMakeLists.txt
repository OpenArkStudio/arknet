add_library(arknet_test_framework STATIC framework.cpp)
target_compile_features(arknet_test_framework PUBLIC cxx_std_20)
target_link_libraries(arknet_test_framework PUBLIC Threads::Threads)

function(arknet_add_check name timeout)
    set(command "arknet_run_check([==[${name}]==] ${timeout}")
    math(EXPR last "${ARGC} - 1")
    foreach(index RANGE 2 ${last})
        set(argument "${ARGV${index}}")
        if(index EQUAL 2 AND TARGET "${argument}")
            set_property(GLOBAL APPEND PROPERTY ARKNET_CHECK_TARGETS "${argument}")
            set(argument "$<TARGET_FILE:${argument}>")
        endif()
        string(APPEND command " [==[${argument}]==]")
    endforeach()
    string(APPEND command ")\n")
    set_property(GLOBAL APPEND_STRING PROPERTY ARKNET_CHECK_COMMANDS "${command}")
endfunction()

function(arknet_configure_check_target)
    get_property(commands GLOBAL PROPERTY ARKNET_CHECK_COMMANDS)
    get_property(targets GLOBAL PROPERTY ARKNET_CHECK_TARGETS)
    file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/tests/check-$<CONFIG>.cmake" CONTENT "${commands}")
    add_custom_target(arknet_check
        COMMAND ${CMAKE_COMMAND}
            "-DARKNET_TEST_COMMANDS=${PROJECT_BINARY_DIR}/tests/check-$<CONFIG>.cmake"
            "-DARKNET_TEST_LOG_DIR=${PROJECT_BINARY_DIR}/tests/logs/$<CONFIG>"
            -P "${PROJECT_SOURCE_DIR}/tests/run.cmake"
        DEPENDS ${targets}
        USES_TERMINAL)
endfunction()

foreach(name tcp udp websocket lifecycle detail concurrency http router io_pool keepalive coroutine_shutdown ipv6)
    add_executable(arknet_${name}_test ${name}.cpp test_main.cpp)
    target_link_libraries(arknet_${name}_test PRIVATE arknet::arknet arknet_test_framework)
    target_compile_definitions(arknet_${name}_test PRIVATE
        ARKNET_TEST_CERT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/certs")
    if(name STREQUAL "concurrency")
        arknet_add_check(${name} 120 arknet_${name}_test)
    elseif(name STREQUAL "http")
        arknet_add_check(${name} 60 arknet_${name}_test)
    else()
        arknet_add_check(${name} 30 arknet_${name}_test)
    endif()
endforeach()
add_executable(arknet_stress_test stress.cpp)
target_link_libraries(arknet_stress_test PRIVATE arknet::arknet arknet_test_framework)
target_compile_definitions(arknet_stress_test PRIVATE
    ARKNET_TEST_CERT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/certs")
arknet_add_check(stress 180 arknet_stress_test --protocol all --seconds 1
    --clients 16 --rounds 2 --window 4 --certs ${CMAKE_CURRENT_SOURCE_DIR}/certs)
if(ARKNET_ENABLE_SSL)
    add_executable(arknet_tls_test tls.cpp test_main.cpp)
    target_link_libraries(arknet_tls_test PRIVATE arknet::arknet arknet_test_framework)
    target_compile_definitions(arknet_tls_test PRIVATE
        ARKNET_TEST_CERT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/certs")
    arknet_add_check(tls 45 arknet_tls_test)
endif()
arknet_add_check(check_runner 15 ${CMAKE_COMMAND}
    -DARKNET_RUNNER_TEST_DIR=${PROJECT_BINARY_DIR}/tests/runner-selfcheck
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_runner_test.cmake)
arknet_add_check(install_consumer 180 ${CMAKE_COMMAND}
    -DARKNET_BUILD_DIR=${PROJECT_BINARY_DIR}
    -DARKNET_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DARKNET_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DARKNET_CONFIG=$<CONFIG>
    -DARKNET_GENERATOR=${CMAKE_GENERATOR}
    -DARKNET_TOOLCHAIN=${CMAKE_TOOLCHAIN_FILE}
    -DARKNET_VCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}
    -DARKNET_VCPKG_TRIPLET=${VCPKG_TARGET_TRIPLET}
    "-DARKNET_DEPENDENCY_PREFIX=${CMAKE_PREFIX_PATH}"
    -DARKNET_ASIO_INCLUDE_DIR=${ARKNET_ASIO_INCLUDE_DIR}
    -DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/install_consumer.cmake)

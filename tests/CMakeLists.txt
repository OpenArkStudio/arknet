add_library(arknet_test_framework STATIC framework.cpp)
set(ARKNET_CHECK_TESTS "" CACHE STRING "Check names to build and run; empty selects all checks")
target_compile_features(arknet_test_framework PUBLIC cxx_std_20)
target_link_libraries(arknet_test_framework PUBLIC Threads::Threads)

function(arknet_add_check name timeout)
    set_property(GLOBAL APPEND PROPERTY ARKNET_REGISTERED_CHECKS "${name}")
    if(ARKNET_CHECK_TESTS AND NOT name IN_LIST ARKNET_CHECK_TESTS)
        return()
    endif()
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
    get_property(registered GLOBAL PROPERTY ARKNET_REGISTERED_CHECKS)
    foreach(name IN LISTS ARKNET_CHECK_TESTS)
        if(NOT name IN_LIST registered)
            message(FATAL_ERROR "Unknown or unavailable arknet check: ${name}")
        endif()
    endforeach()
    get_property(commands GLOBAL PROPERTY ARKNET_CHECK_COMMANDS)
    get_property(targets GLOBAL PROPERTY ARKNET_CHECK_TARGETS)
    file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/tests/check-$<CONFIG>.cmake" CONTENT "${commands}")
    add_custom_target(arknet_check_build DEPENDS ${targets})
    add_custom_target(arknet_check
        COMMAND ${CMAKE_COMMAND}
            "-DARKNET_TEST_COMMANDS=${PROJECT_BINARY_DIR}/tests/check-$<CONFIG>.cmake"
            "-DARKNET_TEST_LOG_DIR=${PROJECT_BINARY_DIR}/tests/logs/$<CONFIG>"
            -P "${PROJECT_SOURCE_DIR}/tests/run.cmake"
        DEPENDS arknet_check_build
        USES_TERMINAL)
endfunction()

set(arknet_test_names tcp udp lifecycle queued_connect_cancellation external_stop detail concurrency io_pool manual_runtime socket_metadata keepalive coroutine_shutdown connection_load_cleanup ipv6 arq_core rudp rudp_stability fec rudp_fec mysql_wire mysql_handshake mysql_command redis_resp redis_client redis_pool redis_shutdown redis_pool_shutdown)
add_executable(arknet_process_resources_test ../benchmarks/process_resources_probe.cpp)
target_compile_features(arknet_process_resources_test PRIVATE cxx_std_20)
target_link_libraries(arknet_process_resources_test PRIVATE Threads::Threads)
if(WIN32)
    target_compile_definitions(arknet_process_resources_test PRIVATE NOMINMAX)
    target_link_libraries(arknet_process_resources_test PRIVATE psapi kernel32)
endif()
arknet_add_check(process_resources 15 arknet_process_resources_test --self-check)
if(ARKNET_USE_BOOST_ASIO)
    list(APPEND arknet_test_names websocket http router httprequest)
endif()
foreach(name IN LISTS arknet_test_names)
    add_executable(arknet_${name}_test ${name}.cpp test_main.cpp)
    target_link_libraries(arknet_${name}_test PRIVATE arknet::arknet arknet_test_framework)
    target_compile_definitions(arknet_${name}_test PRIVATE
        ARKNET_TEST_CERT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/certs")
    if(name STREQUAL "concurrency")
        arknet_add_check(${name} 120 arknet_${name}_test)
    elseif(name STREQUAL "http")
        arknet_add_check(${name} 60 arknet_${name}_test)
    elseif(name STREQUAL "rudp_fec")
        arknet_add_check(${name} 60 arknet_${name}_test)
    elseif(name MATCHES "^redis_(client|pool)$")
        arknet_add_check(${name} 60 arknet_${name}_test)
    else()
        arknet_add_check(${name} 30 arknet_${name}_test)
    endif()
endforeach()
if(WIN32)
    target_link_libraries(arknet_connection_load_cleanup_test PRIVATE psapi kernel32)
endif()
if(ARKNET_ENABLE_SSL)
    target_sources(arknet_redis_shutdown_test PRIVATE redis_shutdown_tls.cpp)
    target_sources(arknet_redis_pool_shutdown_test PRIVATE redis_pool_shutdown_tls.cpp)
endif()
function(arknet_configure_allocation_failure_test target)
    target_link_libraries(${target} PRIVATE arknet::arknet arknet_test_framework)
    # Force a real allocation at Asio submission. The test preload additionally
    # covers MSVC's direct CRT path without changing production allocation.
    target_compile_definitions(${target} PRIVATE
        BOOST_ASIO_DISABLE_SMALL_BLOCK_RECYCLING BOOST_ASIO_DISABLE_STD_ALIGNED_ALLOC BOOST_ASIO_DISABLE_BOOST_ALIGN
        ASIO_DISABLE_SMALL_BLOCK_RECYCLING ASIO_DISABLE_STD_ALIGNED_ALLOC ASIO_DISABLE_BOOST_ALIGN)
endfunction()
add_executable(arknet_allocation_failure_test allocation_failure.cpp scoped_allocation_failure.cpp test_main.cpp)
arknet_configure_allocation_failure_test(arknet_allocation_failure_test)
arknet_add_check(allocation_failure 30 arknet_allocation_failure_test)
add_executable(arknet_redis_close_failure_test redis_close_failure.cpp scoped_allocation_failure.cpp test_main.cpp)
arknet_configure_allocation_failure_test(arknet_redis_close_failure_test)
arknet_add_check(redis_close_failure 30 arknet_redis_close_failure_test)
add_executable(arknet_redis_pool_return_failure_test redis_pool_return_failure.cpp scoped_allocation_failure.cpp test_main.cpp)
arknet_configure_allocation_failure_test(arknet_redis_pool_return_failure_test)
arknet_add_check(redis_pool_return_failure 30 arknet_redis_pool_return_failure_test)
add_executable(arknet_redis_child_close_failure_test redis_child_close_failure.cpp scoped_allocation_failure.cpp test_main.cpp)
arknet_configure_allocation_failure_test(arknet_redis_child_close_failure_test)
arknet_add_check(redis_child_close_failure 30 arknet_redis_child_close_failure_test)
add_executable(arknet_owned_runtime_exit_test owned_runtime_exit.cpp)
target_link_libraries(arknet_owned_runtime_exit_test PRIVATE arknet::arknet)
foreach(mode callback nested global late indirect)
    arknet_add_check(owned_runtime_exit_${mode} 15 arknet_owned_runtime_exit_test ${mode})
endforeach()
foreach(order main_first owner_first)
    set(sources owned_runtime_exit.cpp owned_runtime_exit_owner.cpp)
    if(order STREQUAL "owner_first")
        list(REVERSE sources)
    endif()
    add_executable(arknet_owned_runtime_exit_${order}_test ${sources})
    target_link_libraries(arknet_owned_runtime_exit_${order}_test PRIVATE arknet::arknet)
    target_compile_definitions(arknet_owned_runtime_exit_${order}_test PRIVATE ARKNET_TEST_MULTI_TU)
    arknet_add_check(owned_runtime_exit_${order} 15 arknet_owned_runtime_exit_${order}_test external)
endforeach()
add_executable(arknet_asio_thread_storage_test asio_thread_storage.cpp)
target_link_libraries(arknet_asio_thread_storage_test PRIVATE arknet::arknet)
arknet_add_check(asio_thread_storage 10 arknet_asio_thread_storage_test)
if(NOT ARKNET_USE_BOOST_ASIO)
    foreach(policy DISABLED EXPLICIT PREINCLUDED)
        string(TOLOWER "${policy}" suffix)
        add_executable(arknet_asio_thread_storage_${suffix}_test asio_thread_storage.cpp)
        target_link_libraries(arknet_asio_thread_storage_${suffix}_test PRIVATE arknet::arknet)
        target_compile_definitions(arknet_asio_thread_storage_${suffix}_test PRIVATE ARKNET_TEST_ASIO_STORAGE_${policy})
        arknet_add_check(asio_thread_storage_${suffix} 10 arknet_asio_thread_storage_${suffix}_test)
    endforeach()
endif()
add_executable(arknet_arq_interop_test arq_interop.cpp vendor/kcp/ikcp.c test_main.cpp)
set_source_files_properties(vendor/kcp/ikcp.c PROPERTIES LANGUAGE CXX)
# The reference's null-member offsets also trigger Clang's object-size instrumentation.
set_property(SOURCE vendor/kcp/ikcp.c APPEND PROPERTY COMPILE_OPTIONS
    "$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fno-sanitize=null,object-size>")
target_link_libraries(arknet_arq_interop_test PRIVATE arknet::arknet arknet_test_framework)
arknet_add_check(arq_interop 30 arknet_arq_interop_test)
include(${CMAKE_CURRENT_SOURCE_DIR}/zephyr_interop.cmake)
add_executable(arknet_fec_interop_test fec_interop.cpp vendor/fecpp/reference.cpp test_main.cpp)
target_link_libraries(arknet_fec_interop_test PRIVATE arknet::arknet arknet_test_framework)
arknet_add_check(fec_interop 30 arknet_fec_interop_test)
add_executable(arknet_stress_test stress.cpp)
target_link_libraries(arknet_stress_test PRIVATE arknet::arknet arknet_test_framework)
target_compile_definitions(arknet_stress_test PRIVATE
    ARKNET_TEST_CERT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/certs")
arknet_add_check(stress 180 arknet_stress_test --protocol all --seconds 1
    --clients 16 --rounds 2 --window 4 --certs ${CMAKE_CURRENT_SOURCE_DIR}/certs)
if(ARKNET_ENABLE_SSL)
    add_executable(arknet_mysql_auth_test mysql_auth.cpp test_main.cpp)
    target_link_libraries(arknet_mysql_auth_test PRIVATE arknet::arknet arknet_test_framework)
    arknet_add_check(mysql_auth 30 arknet_mysql_auth_test)
    add_executable(arknet_mysql_connection_test mysql_connection.cpp test_main.cpp)
    target_link_libraries(arknet_mysql_connection_test PRIVATE arknet::arknet arknet_test_framework)
    target_compile_definitions(arknet_mysql_connection_test PRIVATE
        ARKNET_TEST_CERT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/certs")
    arknet_add_check(mysql_connection 60 arknet_mysql_connection_test)
    add_executable(arknet_mysql_shutdown_test mysql_shutdown.cpp test_main.cpp)
    target_link_libraries(arknet_mysql_shutdown_test PRIVATE arknet::arknet arknet_test_framework)
    target_compile_definitions(arknet_mysql_shutdown_test PRIVATE
        ARKNET_TEST_CERT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/certs")
    arknet_add_check(mysql_shutdown 60 arknet_mysql_shutdown_test)
    add_executable(arknet_mysql_pool_return_failure_test mysql_pool_return_failure.cpp scoped_allocation_failure.cpp test_main.cpp)
    arknet_configure_allocation_failure_test(arknet_mysql_pool_return_failure_test)
    target_compile_definitions(arknet_mysql_pool_return_failure_test PRIVATE
        ARKNET_TEST_CERT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/certs")
    arknet_add_check(mysql_pool_return_failure 60 arknet_mysql_pool_return_failure_test)
    add_executable(arknet_mysql_pool_shutdown_test mysql_pool_shutdown.cpp test_main.cpp)
    target_link_libraries(arknet_mysql_pool_shutdown_test PRIVATE arknet::arknet arknet_test_framework)
    target_compile_definitions(arknet_mysql_pool_shutdown_test PRIVATE
        ARKNET_TEST_CERT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/certs")
    arknet_add_check(mysql_pool_shutdown 60 arknet_mysql_pool_shutdown_test)
    add_executable(arknet_mysql_pool_test mysql_pool.cpp test_main.cpp)
    target_link_libraries(arknet_mysql_pool_test PRIVATE arknet::arknet arknet_test_framework)
    target_compile_definitions(arknet_mysql_pool_test PRIVATE
        ARKNET_TEST_CERT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/certs")
    arknet_add_check(mysql_pool 60 arknet_mysql_pool_test)
    add_executable(arknet_tls_test tls.cpp test_main.cpp)
    target_link_libraries(arknet_tls_test PRIVATE arknet::arknet arknet_test_framework)
    target_compile_definitions(arknet_tls_test PRIVATE
        ARKNET_TEST_CERT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/certs")
    arknet_add_check(tls 45 arknet_tls_test)
endif()
arknet_add_check(check_runner 15 ${CMAKE_COMMAND}
    -DARKNET_RUNNER_TEST_DIR=${PROJECT_BINARY_DIR}/tests/runner-selfcheck
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_runner_test.cmake)
arknet_add_check(consumer_launcher 180 ${CMAKE_COMMAND}
    "-DARKNET_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DARKNET_LAUNCHER_TEST_DIR=${PROJECT_BINARY_DIR}/tests/consumer-launcher"
    "-DARKNET_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DARKNET_GENERATOR=${CMAKE_GENERATOR}"
    "-DARKNET_CONFIG=$<CONFIG>"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/consumer_launcher_test.cmake)
arknet_add_check(install_consumer 900 ${CMAKE_COMMAND}
    -DARKNET_BUILD_DIR=${PROJECT_BINARY_DIR}
    -DARKNET_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DARKNET_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    "-DARKNET_CXX_COMPILER_LAUNCHER=${CMAKE_CXX_COMPILER_LAUNCHER}"
    -DARKNET_CONFIG=$<CONFIG>
    -DARKNET_GENERATOR=${CMAKE_GENERATOR}
    -DARKNET_TOOLCHAIN=${CMAKE_TOOLCHAIN_FILE}
    -DARKNET_VCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}
    -DARKNET_VCPKG_TRIPLET=${VCPKG_TARGET_TRIPLET}
    "-DARKNET_DEPENDENCY_PREFIX=${CMAKE_PREFIX_PATH}"
    -DARKNET_ASIO_INCLUDE_DIR=${ARKNET_ASIO_INCLUDE_DIR}
    -DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/install_consumer.cmake)

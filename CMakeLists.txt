cmake_minimum_required(VERSION 3.21)
project(arknet VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)

option(ARKNET_USE_BOOST_ASIO "Use Boost.Asio and Boost.Beast" ON)
option(ARKNET_ENABLE_SSL "Enable TLS, HTTPS and secure WebSocket support" OFF)
option(ARKNET_BUILD_TESTS "Build loopback tests" ${PROJECT_IS_TOP_LEVEL})
option(ARKNET_BUILD_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})
option(ARKNET_BUILD_BENCHMARKS "Build loopback performance benchmarks" OFF)

set(_ARKNET_DEPENDENCIES_REQUIRED REQUIRED)
include(cmake/arknetDependencies.cmake)
unset(_ARKNET_DEPENDENCIES_REQUIRED)
add_library(arknet INTERFACE)
add_library(arknet::arknet ALIAS arknet)
target_compile_features(arknet INTERFACE cxx_std_20)
target_compile_options(arknet INTERFACE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
target_include_directories(arknet INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(arknet INTERFACE Threads::Threads)
if(ARKNET_USE_BOOST_ASIO)
    target_compile_definitions(arknet INTERFACE ARKNET_USE_BOOST_ASIO)
    target_link_libraries(arknet INTERFACE Boost::headers)
else()
    target_compile_definitions(arknet INTERFACE ARKNET_USE_BOOST_ASIO=0)
    target_link_libraries(arknet INTERFACE arknet::asio)
endif()
if(ARKNET_ENABLE_SSL)
    target_compile_definitions(arknet INTERFACE ARKNET_ENABLE_SSL)
    target_link_libraries(arknet INTERFACE OpenSSL::SSL OpenSSL::Crypto)
endif()
if(WIN32)
    target_link_libraries(arknet INTERFACE ws2_32 mswsock)
endif()

include(CMakePackageConfigHelpers)
install(TARGETS arknet EXPORT arknetTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT arknetTargets NAMESPACE arknet:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/arknet)
configure_package_config_file(cmake/arknetConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/arknetConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/arknet)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/arknetConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/arknetConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/arknetConfigVersion.cmake cmake/arknetDependencies.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/arknet)
install(FILES README.md README_CN.md LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/arknet)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DATADIR}/arknet/docs
    FILES_MATCHING PATTERN "*.md" PATTERN "*.html" PATTERN "*.png" PATTERN "*.svg" PATTERN "*.json" PATTERN ".nojekyll"
    PATTERN "site" EXCLUDE)

if(ARKNET_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(ARKNET_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(ARKNET_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
    if(ARKNET_BUILD_TESTS)
        set(_arknet_benchmark_protocols tcp udp)
        if(ARKNET_USE_BOOST_ASIO)
            list(APPEND _arknet_benchmark_protocols websocket http)
        endif()
        if(ARKNET_ENABLE_SSL)
            list(APPEND _arknet_benchmark_protocols tcps)
            if(ARKNET_USE_BOOST_ASIO)
                list(APPEND _arknet_benchmark_protocols wss https)
            endif()
        endif()
        foreach(protocol IN LISTS _arknet_benchmark_protocols)
            arknet_add_check(benchmark_${protocol} 45 arknet_loopback_benchmark
                --protocol ${protocol} --payload 1024 --clients 2 --window 1
                --seconds 0.2 --warmup 0.1 --certs ${PROJECT_SOURCE_DIR}/tests/certs)
            arknet_add_check(benchmark_${protocol}_shared4 45 arknet_loopback_benchmark
                --protocol ${protocol} --payload 1024 --clients 16 --window 1
                --seconds 0.2 --warmup 0.1 --io-model shared --io-threads 4
                --certs ${PROJECT_SOURCE_DIR}/tests/certs)
            arknet_add_check(benchmark_${protocol}_sharded4 45 arknet_loopback_benchmark
                --protocol ${protocol} --payload 1024 --clients 16 --window 1
                --seconds 0.2 --warmup 0.1 --io-model sharded --io-threads 4
                --certs ${PROJECT_SOURCE_DIR}/tests/certs)
        endforeach()
        unset(_arknet_benchmark_protocols)
        if(WIN32 AND ARKNET_USE_BOOST_ASIO AND ARKNET_ENABLE_SSL)
            foreach(round RANGE 1 32)
                arknet_add_check(benchmark_https_shared4_repeat${round} 45 arknet_loopback_benchmark
                    --protocol https --payload 1024 --clients 16 --window 1
                    --seconds 0.2 --warmup 0.1 --io-model shared --io-threads 4
                    --certs ${PROJECT_SOURCE_DIR}/tests/certs)
            endforeach()
        endif()
        arknet_add_check(benchmark_rudp 45 arknet_rudp_benchmark
            --payload 1024 --clients 2 --window 4 --seconds 0.2 --warmup 0.1 --rounds 1)
        foreach(model shared sharded)
            arknet_add_check(benchmark_rudp_${model}4 45 arknet_rudp_benchmark
                --payload 1024 --clients 4 --window 4 --seconds 0.2 --warmup 0.1 --rounds 1
                --io-model ${model} --io-threads 4)
        endforeach()
        foreach(model shared sharded)
            arknet_add_check(coroutine_${model} 45 arknet_coroutine_benchmark
                --protocol tcp --payload 1024 --clients 16 --window 1
                --seconds 0.2 --warmup 0.1 --io-model ${model} --io-threads 4 --work 0)
            arknet_add_check(coroutine_${model}_cpu 45 arknet_coroutine_benchmark
                --protocol tcp --payload 1024 --clients 16 --window 1
                --seconds 0.2 --warmup 0.1 --io-model ${model} --io-threads 4 --work 10000)
        endforeach()
        foreach(policy off fixed adaptive)
            arknet_add_check(benchmark_rudp_${policy}_loss 45 arknet_rudp_benchmark
                --payload 1024 --clients 2 --window 8 --seconds 0.2 --warmup 0.1 --rounds 1
                --io-model shared --io-threads 4 --fec ${policy} --profile random
                --loss-percent 3 --delay-ms 2 --duplicate-percent 1 --reorder-percent 5 --seed 42)
        endforeach()
    endif()
endif()
if(ARKNET_BUILD_TESTS)
    arknet_configure_check_target()
endif()

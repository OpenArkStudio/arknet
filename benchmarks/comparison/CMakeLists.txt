# Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
cmake_minimum_required(VERSION 3.21)
project(arknet_benchmark_comparison LANGUAGES CXX)

set(ARKNET_LIBRARY_SOURCE "" CACHE PATH "Library checkout to measure")
set(ARKNET_BENCHMARK_DRIVER_SOURCE "" CACHE PATH "Shared benchmark driver checkout")
if(NOT EXISTS "${ARKNET_LIBRARY_SOURCE}/CMakeLists.txt" OR
   NOT IS_DIRECTORY "${ARKNET_LIBRARY_SOURCE}/include/arknet")
    message(FATAL_ERROR "Set ARKNET_LIBRARY_SOURCE to an arknet source checkout")
endif()

set(drivers loopback coroutine redis)
if(ARKNET_ENABLE_SSL)
    list(APPEND drivers mysql)
endif()
foreach(driver IN LISTS drivers)
    if(NOT EXISTS "${ARKNET_BENCHMARK_DRIVER_SOURCE}/benchmarks/${driver}.cpp")
        message(FATAL_ERROR "ARKNET_BENCHMARK_DRIVER_SOURCE must contain benchmarks/${driver}.cpp")
    endif()
endforeach()

set(ARKNET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ARKNET_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ARKNET_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory("${ARKNET_LIBRARY_SOURCE}" library)

foreach(driver IN LISTS drivers)
    add_executable(arknet_${driver}_benchmark
        "${ARKNET_BENCHMARK_DRIVER_SOURCE}/benchmarks/${driver}.cpp")
    target_link_libraries(arknet_${driver}_benchmark PRIVATE arknet::arknet)
    set_target_properties(arknet_${driver}_benchmark PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/benchmarks")
    if(WIN32)
        target_link_libraries(arknet_${driver}_benchmark PRIVATE psapi)
    endif()
endforeach()

if(WIN32)
    target_link_libraries(arknet_loopback_benchmark PRIVATE dbghelp)
    if(MSVC)
        target_compile_options(arknet_loopback_benchmark PRIVATE $<$<CONFIG:Release>:/Z7>)
        target_link_options(arknet_loopback_benchmark PRIVATE $<$<CONFIG:Release>:/DEBUG>
            $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
    endif()
endif()

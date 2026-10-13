set(ARKNET_ZEPHYR_ROOT "" CACHE PATH "Optional zephyr checkout for RDP endpoint interoperability tests")
if(ARKNET_ZEPHYR_ROOT STREQUAL "")
    return()
endif()

set(_arknet_zephyr_include "${ARKNET_ZEPHYR_ROOT}/include")
foreach(header zephyr/udp/udp_client.hpp zephyr/udp/udp_session.hpp zephyr/udp/udp_server.hpp zephyr/base/detail/util.hpp)
    if(NOT EXISTS "${_arknet_zephyr_include}/${header}")
        message(FATAL_ERROR "ARKNET_ZEPHYR_ROOT must contain include/${header}")
    endif()
endforeach()

# Apply the same resolver compatibility fix as ARK, only in the test build tree.
set(_arknet_zephyr_util "${_arknet_zephyr_include}/zephyr/base/detail/util.hpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_arknet_zephyr_util}")
file(READ "${_arknet_zephyr_util}" _arknet_zephyr_util_content)
set(_arknet_zephyr_legacy_resolve "*resolver.resolve(h, p, asio::ip::resolver_base::flags::address_configured)")
set(_arknet_zephyr_modern_resolve "${_arknet_zephyr_legacy_resolve}.begin()")
string(REPLACE "${_arknet_zephyr_modern_resolve}" "${_arknet_zephyr_legacy_resolve}"
    _arknet_zephyr_util_content "${_arknet_zephyr_util_content}")
string(REPLACE "${_arknet_zephyr_legacy_resolve}" "${_arknet_zephyr_modern_resolve}"
    _arknet_zephyr_util_content "${_arknet_zephyr_util_content}")
set(_arknet_zephyr_overlay "${CMAKE_CURRENT_BINARY_DIR}/zephyr-interop-compat")
file(GENERATE OUTPUT "${_arknet_zephyr_overlay}/zephyr/base/detail/util.hpp"
    CONTENT "${_arknet_zephyr_util_content}")
# Qualify inherited constructors for Clang in the same build-only overlay.
foreach(endpoint udp_client udp_session)
    set(_arknet_zephyr_endpoint "${_arknet_zephyr_include}/zephyr/udp/${endpoint}.hpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_arknet_zephyr_endpoint}")
    file(READ "${_arknet_zephyr_endpoint}" _arknet_zephyr_endpoint_content)
    string(REPLACE "using ${endpoint}_impl_t<" "using detail::${endpoint}_impl_t<"
        _arknet_zephyr_endpoint_content "${_arknet_zephyr_endpoint_content}")
    file(GENERATE OUTPUT "${_arknet_zephyr_overlay}/zephyr/udp/${endpoint}.hpp"
        CONTENT "${_arknet_zephyr_endpoint_content}")
endforeach()

add_executable(arknet_rudp_interop_peer interop/rudp_peer.cpp)
target_link_libraries(arknet_rudp_interop_peer PRIVATE arknet::arknet)
add_executable(arknet_zephyr_rdp_peer interop/rudp_peer.cpp)
target_compile_features(arknet_zephyr_rdp_peer PRIVATE cxx_std_17)
target_compile_definitions(arknet_zephyr_rdp_peer PRIVATE ARKNET_INTEROP_LEGACY)
target_include_directories(arknet_zephyr_rdp_peer PRIVATE
    "${_arknet_zephyr_overlay}" "${_arknet_zephyr_include}")
target_link_libraries(arknet_zephyr_rdp_peer PRIVATE Threads::Threads)
if(ARKNET_USE_BOOST_ASIO)
    target_compile_definitions(arknet_zephyr_rdp_peer PRIVATE BOOST_ASIO_NO_DEPRECATED)
    target_link_libraries(arknet_zephyr_rdp_peer PRIVATE Boost::headers)
else()
    target_compile_definitions(arknet_zephyr_rdp_peer PRIVATE ASIO_STANDALONE ASIO_NO_DEPRECATED)
    target_link_libraries(arknet_zephyr_rdp_peer PRIVATE arknet::asio)
endif()
if(WIN32)
    target_compile_options(arknet_zephyr_rdp_peer PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
    target_link_libraries(arknet_zephyr_rdp_peer PRIVATE ws2_32 mswsock)
endif()
# The legacy KCP header uses null-member offsets, like tests/vendor/kcp/ikcp.c.
target_compile_options(arknet_zephyr_rdp_peer PRIVATE
    "$<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-fno-sanitize=null,object-size>")

set(_arknet_zephyr_check_arguments
    "-DARKNET_CURRENT_PEER=$<TARGET_FILE:arknet_rudp_interop_peer>"
    "-DARKNET_LEGACY_PEER=$<TARGET_FILE:arknet_zephyr_rdp_peer>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/interop/run.cmake")
set_property(GLOBAL APPEND PROPERTY ARKNET_CHECK_TARGETS
    arknet_rudp_interop_peer arknet_zephyr_rdp_peer)
arknet_add_check(zephyr_rdp_interop 600 ${CMAKE_COMMAND} ${_arknet_zephyr_check_arguments})
add_custom_target(arknet_zephyr_interop_check
    COMMAND ${CMAKE_COMMAND} ${_arknet_zephyr_check_arguments}
    DEPENDS arknet_rudp_interop_peer arknet_zephyr_rdp_peer
    USES_TERMINAL)
unset(_arknet_zephyr_check_arguments)
unset(_arknet_zephyr_include)
unset(_arknet_zephyr_overlay)
unset(_arknet_zephyr_util)
unset(_arknet_zephyr_util_content)
unset(_arknet_zephyr_legacy_resolve)
unset(_arknet_zephyr_modern_resolve)
unset(_arknet_zephyr_endpoint)
unset(_arknet_zephyr_endpoint_content)

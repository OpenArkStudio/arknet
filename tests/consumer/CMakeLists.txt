cmake_minimum_required(VERSION 3.20)
project(arknet_consumer LANGUAGES CXX)
find_package(arknet CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE arknet::arknet)
set(public_headers
    base/io_pool.hpp base/timer.hpp
    tcp/tcp_client.hpp tcp/tcp_server.hpp tcp/tcp_session.hpp tcp/tcp_stream.hpp
    udp/udp_client.hpp udp/udp_server.hpp udp/udp_session.hpp udp/udp_cast.hpp
    websocket/ws_client.hpp websocket/ws_server.hpp websocket/ws_session.hpp
    http/http_client.hpp http/http_server.hpp http/http_session.hpp http/router.hpp
    util/spin_lock.hpp util/string.hpp)
if(ARKNET_ENABLE_SSL)
    list(APPEND public_headers
        tcp/tcps_client.hpp tcp/tcps_server.hpp tcp/tcps_session.hpp
        websocket/wss_client.hpp websocket/wss_server.hpp websocket/wss_session.hpp
        http/https_client.hpp http/https_server.hpp http/https_session.hpp)
endif()
foreach(header IN LISTS public_headers)
    string(REPLACE "/" "_" unit "${header}")
    set(source "${CMAKE_CURRENT_BINARY_DIR}/headers/${unit}.cpp")
    file(GENERATE OUTPUT "${source}" CONTENT "#include <arknet/${header}>\n")
    target_sources(consumer PRIVATE "${source}")
endforeach()
add_custom_target(check_consumer COMMAND consumer DEPENDS consumer)

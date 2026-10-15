#include <arknet/arknet.hpp>
#include <arknet/redis/client.hpp>
#include <arknet/redis/pool.hpp>

#ifdef ARKNET_ENABLE_SSL
#include <arknet/mysql/connection.hpp>
#include <arknet/mysql/pool.hpp>
#endif

#if !ARKNET_USE_BOOST_ASIO && (defined(BOOST_ASIO_VERSION) || defined(BOOST_BEAST_VERSION))
#error "Standalone protocol headers must not depend on Boost"
#endif

int installed_fec_selfcheck();

int main() {
    arknet::tcp_client tcp;
    arknet::udp_client udp;
    arknet::redis::client redis;
    arknet::redis::pool redis_pool;
#if ARKNET_USE_BOOST_ASIO
    arknet::ws_client websocket;
    arknet::http_client http;
    arknet::http_server http_server;
#endif
    arknet::rudp_client reliable_client;
    arknet::rudp_server reliable_server;
    reliable_client.set_fec_options(arknet::fec::options::adaptive());
#ifdef ARKNET_ENABLE_SSL
    arknet::tcps_client tls;
    arknet::mysql::connection mysql;
    arknet::mysql::pool mysql_pool;
#if ARKNET_USE_BOOST_ASIO
    arknet::wss_client secure_websocket;
    arknet::https_client https;
    arknet::https_server https_server;
#endif
#endif
    return installed_fec_selfcheck();
}

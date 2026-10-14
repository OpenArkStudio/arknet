#include <arknet/arknet.hpp>

int installed_fec_selfcheck();

int main() {
    arknet::tcp_client tcp;
    arknet::udp_client udp;
    arknet::ws_client websocket;
    arknet::http_client http;
    arknet::http_server http_server;
    arknet::rudp_client reliable_client;
    arknet::rudp_server reliable_server;
    reliable_client.set_fec_options(arknet::fec::options::adaptive());
#ifdef ARKNET_ENABLE_SSL
    arknet::tcps_client tls;
    arknet::wss_client secure_websocket;
    arknet::https_client https;
    arknet::https_server https_server;
#endif
    return installed_fec_selfcheck();
}

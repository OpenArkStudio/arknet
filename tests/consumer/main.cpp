#include <arknet/arknet.hpp>

int main()
{
    arknet::tcp_client tcp;
    arknet::udp_client udp;
    arknet::ws_client websocket;
    arknet::http_client http;
    arknet::http_server http_server;
#ifdef ARKNET_ENABLE_SSL
    arknet::tcps_client tls;
    arknet::wss_client secure_websocket;
    arknet::https_client https;
    arknet::https_server https_server;
#endif
}

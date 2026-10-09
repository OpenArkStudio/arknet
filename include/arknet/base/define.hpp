// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

// Components cooperate through the CRTP endpoint's protected state.
#define ARKNET_DETAIL_COMPONENT(K, name) template <class, class> K name;
#define ARKNET_DETAIL_BASE_ACCESS(K)                                                                                   \
    K io_t;                                                                                                            \
    K io_pool;                                                                                                         \
    K data_filter_before_helper;                                                                                       \
    template <class> K event_queue_guard;                                                                              \
    template <class> K session_mgr_t;                                                                                  \
    ARKNET_DETAIL_COMPONENT(K, iopool_cp)                                                                              \
    ARKNET_DETAIL_COMPONENT(K, alive_time_cp)                                                                          \
    ARKNET_DETAIL_COMPONENT(K, condition_event_cp)                                                                     \
    ARKNET_DETAIL_COMPONENT(K, connect_cp)                                                                             \
    ARKNET_DETAIL_COMPONENT(K, connect_time_cp)                                                                        \
    ARKNET_DETAIL_COMPONENT(K, connect_timeout_cp)                                                                     \
    ARKNET_DETAIL_COMPONENT(K, data_persistence_cp)                                                                    \
    ARKNET_DETAIL_COMPONENT(K, shutdown_cp)                                                                            \
    ARKNET_DETAIL_COMPONENT(K, close_cp)                                                                               \
    ARKNET_DETAIL_COMPONENT(K, disconnect_cp)                                                                          \
    ARKNET_DETAIL_COMPONENT(K, event_queue_cp)                                                                         \
    ARKNET_DETAIL_COMPONENT(K, post_cp)                                                                                \
    ARKNET_DETAIL_COMPONENT(K, reconnect_timer_cp)                                                                     \
    ARKNET_DETAIL_COMPONENT(K, send_cp)                                                                                \
    ARKNET_DETAIL_COMPONENT(K, silence_timer_cp)                                                                       \
    ARKNET_DETAIL_COMPONENT(K, socket_cp)                                                                              \
    ARKNET_DETAIL_COMPONENT(K, user_data_cp)                                                                           \
    ARKNET_DETAIL_COMPONENT(K, user_timer_cp)                                                                          \
    ARKNET_DETAIL_COMPONENT(K, thread_id_cp)                                                                           \
    static_assert(true)
#define ARKNET_DETAIL_STREAM_ACCESS(K)                                                                                 \
    ARKNET_DETAIL_COMPONENT(K, ssl_context_cp)                                                                         \
    ARKNET_DETAIL_COMPONENT(K, ssl_stream_cp)                                                                          \
    ARKNET_DETAIL_COMPONENT(K, tcp_keepalive_cp)                                                                       \
    ARKNET_DETAIL_COMPONENT(K, tcp_recv_op)                                                                            \
    ARKNET_DETAIL_COMPONENT(K, tcp_send_op)                                                                            \
    ARKNET_DETAIL_COMPONENT(K, ws_stream_cp)                                                                           \
    ARKNET_DETAIL_COMPONENT(K, ws_send_op)                                                                             \
    static_assert(true)
#define ARKNET_DETAIL_CLIENT_ACCESS(K)                                                                                 \
    ARKNET_DETAIL_COMPONENT(K, client_impl_t)                                                                          \
    ARKNET_DETAIL_COMPONENT(K, tcp_client_impl_t)                                                                      \
    ARKNET_DETAIL_COMPONENT(K, tcps_client_impl_t)                                                                     \
    ARKNET_DETAIL_COMPONENT(K, ws_client_impl_t)                                                                       \
    ARKNET_DETAIL_COMPONENT(K, wss_client_impl_t)                                                                      \
    static_assert(true)
#define ARKNET_DETAIL_SERVER_ACCESS(K)                                                                                 \
    ARKNET_DETAIL_COMPONENT(K, server_impl_t)                                                                          \
    ARKNET_DETAIL_COMPONENT(K, tcp_server_impl_t)                                                                      \
    ARKNET_DETAIL_COMPONENT(K, tcps_server_impl_t)                                                                     \
    ARKNET_DETAIL_COMPONENT(K, ws_server_impl_t)                                                                       \
    ARKNET_DETAIL_COMPONENT(K, wss_server_impl_t)                                                                      \
    static_assert(true)
#define ARKNET_DETAIL_SESSION_ACCESS(K)                                                                                \
    ARKNET_DETAIL_COMPONENT(K, session_impl_t)                                                                         \
    ARKNET_DETAIL_COMPONENT(K, tcp_session_impl_t)                                                                     \
    ARKNET_DETAIL_COMPONENT(K, tcps_session_impl_t)                                                                    \
    ARKNET_DETAIL_COMPONENT(K, ws_session_impl_t)                                                                      \
    ARKNET_DETAIL_COMPONENT(K, wss_session_impl_t)                                                                     \
    static_assert(true)
#define ARKNET_DETAIL_DATAGRAM_ACCESS(K)                                                                               \
    ARKNET_DETAIL_COMPONENT(K, udp_send_cp)                                                                            \
    ARKNET_DETAIL_COMPONENT(K, udp_send_op)                                                                            \
    ARKNET_DETAIL_COMPONENT(K, udp_recv_op)                                                                            \
    static_assert(true)
#define ARKNET_DETAIL_UDP_CLIENT_ACCESS(K)                                                                             \
    ARKNET_DETAIL_COMPONENT(K, client_impl_t)                                                                          \
    ARKNET_DETAIL_COMPONENT(K, udp_client_impl_t)                                                                      \
    ARKNET_DETAIL_COMPONENT(K, udp_cast_impl_t)                                                                        \
    static_assert(true)
#define ARKNET_DETAIL_UDP_SERVER_ACCESS(K)                                                                             \
    ARKNET_DETAIL_COMPONENT(K, server_impl_t)                                                                          \
    ARKNET_DETAIL_COMPONENT(K, udp_server_impl_t)                                                                      \
    static_assert(true)
#define ARKNET_DETAIL_UDP_SESSION_ACCESS(K)                                                                            \
    ARKNET_DETAIL_COMPONENT(K, session_impl_t)                                                                         \
    ARKNET_DETAIL_COMPONENT(K, udp_session_impl_t)                                                                     \
    static_assert(true)

#define ARKNET_CLASS_FORWARD_DECLARE_BASE ARKNET_DETAIL_BASE_ACCESS(class)
#define ARKNET_CLASS_FRIEND_DECLARE_BASE ARKNET_DETAIL_BASE_ACCESS(friend class)
#define ARKNET_CLASS_FORWARD_DECLARE_TCP_BASE ARKNET_DETAIL_STREAM_ACCESS(class)
#define ARKNET_CLASS_FRIEND_DECLARE_TCP_BASE ARKNET_DETAIL_STREAM_ACCESS(friend class)
#define ARKNET_CLASS_FORWARD_DECLARE_TCP_CLIENT ARKNET_DETAIL_CLIENT_ACCESS(class)
#define ARKNET_CLASS_FRIEND_DECLARE_TCP_CLIENT ARKNET_DETAIL_CLIENT_ACCESS(friend class)
#define ARKNET_CLASS_FORWARD_DECLARE_TCP_SERVER ARKNET_DETAIL_SERVER_ACCESS(class)
#define ARKNET_CLASS_FRIEND_DECLARE_TCP_SERVER ARKNET_DETAIL_SERVER_ACCESS(friend class)
#define ARKNET_CLASS_FORWARD_DECLARE_TCP_SESSION ARKNET_DETAIL_SESSION_ACCESS(class)
#define ARKNET_CLASS_FRIEND_DECLARE_TCP_SESSION ARKNET_DETAIL_SESSION_ACCESS(friend class)
#define ARKNET_CLASS_FORWARD_DECLARE_UDP_BASE ARKNET_DETAIL_DATAGRAM_ACCESS(class)
#define ARKNET_CLASS_FRIEND_DECLARE_UDP_BASE ARKNET_DETAIL_DATAGRAM_ACCESS(friend class)
#define ARKNET_CLASS_FORWARD_DECLARE_UDP_CLIENT ARKNET_DETAIL_UDP_CLIENT_ACCESS(class)
#define ARKNET_CLASS_FRIEND_DECLARE_UDP_CLIENT ARKNET_DETAIL_UDP_CLIENT_ACCESS(friend class)
#define ARKNET_CLASS_FORWARD_DECLARE_UDP_SERVER ARKNET_DETAIL_UDP_SERVER_ACCESS(class)
#define ARKNET_CLASS_FRIEND_DECLARE_UDP_SERVER ARKNET_DETAIL_UDP_SERVER_ACCESS(friend class)
#define ARKNET_CLASS_FORWARD_DECLARE_UDP_SESSION ARKNET_DETAIL_UDP_SESSION_ACCESS(class)
#define ARKNET_CLASS_FRIEND_DECLARE_UDP_SESSION ARKNET_DETAIL_UDP_SESSION_ACCESS(friend class)

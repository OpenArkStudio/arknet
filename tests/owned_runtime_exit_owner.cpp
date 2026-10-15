// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <memory>

#include <arknet/base/io_pool.hpp>

namespace {

std::shared_ptr<arknet::detail::io_pool> external_owner;
#if defined(ARKNET_ENABLE_SSL)
std::unique_ptr<asio::ssl::context> external_tls;
#endif

} // namespace

// A different translation unit constructs its generic owner before main and
// acquires the pool later. Link this object both before and after the main TU.
void arknet_test_hold_external_runtime() {
    external_owner.reset(new arknet::detail::io_pool(1), arknet::detail::iopool_deleter{});
    external_owner->start();
#if defined(ARKNET_ENABLE_SSL)
    external_tls = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_client);
#endif
}

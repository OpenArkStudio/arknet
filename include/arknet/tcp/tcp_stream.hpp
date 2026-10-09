// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once

#include <arknet/base/basic_stream.hpp>

namespace arknet
{
template <class Policy = unlimited_rate_policy>
using tcp_stream = basic_stream<asio::ip::tcp, asio::any_io_executor, Policy>;
}

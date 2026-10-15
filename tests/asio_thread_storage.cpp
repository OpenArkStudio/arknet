// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <string_view>

// Exercise caller configuration before the public dependency header is read.
#if defined(ARKNET_TEST_ASIO_STORAGE_DISABLED)
#define ASIO_DISABLE_THREAD_KEYWORD_EXTENSION
#elif defined(ARKNET_TEST_ASIO_STORAGE_EXPLICIT)
#define ASIO_STANDALONE
#include <asio/detail/config.hpp>

#undef ASIO_HAS_THREAD_KEYWORD_EXTENSION
#undef ASIO_THREAD_KEYWORD
#define ASIO_HAS_THREAD_KEYWORD_EXTENSION 1
#define ASIO_THREAD_KEYWORD thread_local
#elif defined(ARKNET_TEST_ASIO_STORAGE_PREINCLUDED)
#define ASIO_STANDALONE
#define ASIO_DISABLE_THREAD_KEYWORD_EXTENSION
#include <asio.hpp>

#undef ASIO_DISABLE_THREAD_KEYWORD_EXTENSION
#endif

#include <arknet/external/asio.hpp>

#define ARKNET_TEST_STRINGIZE_IMPL(value) #value
#define ARKNET_TEST_STRINGIZE(value) ARKNET_TEST_STRINGIZE_IMPL(value)

#if defined(ARKNET_USE_BOOST_ASIO) && ARKNET_USE_BOOST_ASIO
#if !defined(BOOST_ASIO_HAS_THREAD_KEYWORD_EXTENSION)
#error "default Boost.Asio thread storage should remain native"
#endif
#elif defined(ARKNET_TEST_ASIO_STORAGE_DISABLED) || defined(ARKNET_TEST_ASIO_STORAGE_PREINCLUDED)
#if defined(ASIO_HAS_THREAD_KEYWORD_EXTENSION)
#error "arknet must preserve the caller's legacy thread-storage configuration"
#endif
#elif defined(ARKNET_TEST_ASIO_STORAGE_EXPLICIT)
static_assert(std::string_view(ARKNET_TEST_STRINGIZE(ASIO_THREAD_KEYWORD)) == "thread_local");
#else
#if !defined(ASIO_HAS_THREAD_KEYWORD_EXTENSION)
#error "default Standalone Asio must use native thread storage for owned-runtime exit"
#endif
#endif

int main() {}

// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
#include <string>
#include <string_view>
#include <arknet/base/error.hpp>

namespace arknet::detail {
template<class Derived, class Args>
class ssl_context_cp : public asio::ssl::context {
public:
    explicit ssl_context_cp(asio::ssl::context::method method) : asio::ssl::context(method) {
        set_options(default_workarounds | no_sslv2 | no_sslv3);
        if (SSL_CTX_set_min_proto_version(native_handle(), TLS1_2_VERSION) != 1)
            throw system_error(asio::error::invalid_argument, "Unable to require TLS 1.2");
        if constexpr (Args::is_client) {
            set_verify_mode(asio::ssl::verify_peer);
            set_default_verify_paths();
        }
    }
    Derived& set_cert_buffer(std::string_view ca, std::string_view certificate,
        std::string_view key, std::string_view password) noexcept {
        return load_identity(password, [&, this](error_code& ec) {
            if (certificate.empty() || key.empty()) { ec = asio::error::invalid_argument; return; }
            use_certificate_chain(asio::buffer(certificate), ec);
            if (!ec) use_private_key(asio::buffer(key), pem, ec);
            if (!ec && !ca.empty()) add_certificate_authority(asio::buffer(ca), ec);
        });
    }
    Derived& set_cert_file(const std::string& ca, const std::string& certificate,
        const std::string& key, const std::string& password) noexcept {
        return load_identity(password, [&, this](error_code& ec) {
            if (certificate.empty() || key.empty()) { ec = asio::error::invalid_argument; return; }
            use_certificate_chain_file(certificate, ec);
            if (!ec) use_private_key_file(key, pem, ec);
            if (!ec && !ca.empty()) load_verify_file(ca, ec);
        });
    }
    Derived& set_dh_buffer(std::string_view parameters) noexcept {
        error_code ec;
        if (!parameters.empty()) use_tmp_dh(asio::buffer(parameters), ec);
        set_last_error(ec);
        return static_cast<Derived&>(*this);
    }
    Derived& set_dh_file(const std::string& path) noexcept {
        error_code ec;
        if (!path.empty()) use_tmp_dh_file(path, ec);
        set_last_error(ec);
        return static_cast<Derived&>(*this);
    }
protected:
    error_code _check_tls_identity() noexcept {
        auto* context = native_handle();
        if (!SSL_CTX_get0_certificate(context) || !SSL_CTX_get0_privatekey(context))
            return asio::error::invalid_argument;
        ERR_clear_error();
        if (SSL_CTX_check_private_key(context) == 1) return {};
        const int native = static_cast<int>(ERR_get_error());
        return native ? error_code{native, asio::error::get_ssl_category()} : error_code{asio::error::invalid_argument};
    }
    error_code identity_error_;
private:
    template<class Load> Derived& load_identity(std::string_view password, Load load) noexcept {
        error_code ec;
        set_password_callback([secret = std::string{password}](std::size_t, password_purpose) { return secret; }, ec);
        if (!ec) load(ec);
        if (!ec) ec = _check_tls_identity();
        identity_error_ = ec;
        set_last_error(ec);
        return static_cast<Derived&>(*this);
    }
};
}
#endif

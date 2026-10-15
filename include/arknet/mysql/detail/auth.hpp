// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <openssl/crypto.h>
#include <openssl/evp.h>

namespace arknet::mysql::detail {

enum class auth_status
{
    complete,
    unsupported_plugin,
    invalid_challenge,
    crypto_error
};

namespace auth_detail {

inline bool digest(const EVP_MD* algorithm, std::initializer_list<std::string_view> parts, unsigned char* output,
    unsigned int expected_size) {
    const auto context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!context || !algorithm || EVP_DigestInit_ex(context.get(), algorithm, nullptr) != 1)
    {
        return false;
    }
    for (auto part : parts)
    {
        if (!part.empty() && EVP_DigestUpdate(context.get(), part.data(), part.size()) != 1)
        {
            return false;
        }
    }
    unsigned int written = 0;
    return EVP_DigestFinal_ex(context.get(), output, &written) == 1 && written == expected_size;
}

struct hash_storage
{
    std::array<unsigned char, 32> first{};
    std::array<unsigned char, 32> second{};
    std::array<unsigned char, 32> third{};

    ~hash_storage() {
        OPENSSL_cleanse(first.data(), first.size());
        OPENSSL_cleanse(second.data(), second.size());
        OPENSSL_cleanse(third.data(), third.size());
    }
};

} // namespace auth_detail

// Implements challenge responses only. Full caching_sha2 authentication must use
// a verified TLS connection; this function never sends a plaintext password.
inline auth_status make_auth_response(
    std::string_view plugin, std::string_view password, std::string_view challenge, std::string& output) {
    const bool native = plugin == "mysql_native_password";
    if (!native && plugin != "caching_sha2_password")
    {
        return auth_status::unsupported_plugin;
    }
    if (password.empty())
    {
        output.clear();
        return auth_status::complete;
    }
    if (challenge.size() != 20)
    {
        return auth_status::invalid_challenge;
    }
    const EVP_MD* algorithm = native ? EVP_sha1() : EVP_sha256();
    const unsigned int size = native ? 20 : 32;
    auth_detail::hash_storage hashes;
    const auto bytes = [size](const auto& hash) {
        return std::string_view(reinterpret_cast<const char*>(hash.data()), size);
    };
    if (!auth_detail::digest(algorithm, {password}, hashes.first.data(), size) ||
        !auth_detail::digest(algorithm, {bytes(hashes.first)}, hashes.second.data(), size))
    {
        return auth_status::crypto_error;
    }
    // Native: SHA1(challenge || SHA1(SHA1(password))).
    // Caching SHA2: SHA256(SHA256(SHA256(password)) || challenge).
    if (!auth_detail::digest(algorithm,
            native ? std::initializer_list<std::string_view>{challenge, bytes(hashes.second)}
                   : std::initializer_list<std::string_view>{bytes(hashes.second), challenge},
            hashes.third.data(), size))
    {
        return auth_status::crypto_error;
    }
    std::string response(size, '\0');
    for (unsigned int i = 0; i < size; ++i)
    {
        response[i] = static_cast<char>(hashes.first[i] ^ hashes.third[i]);
    }
    output = std::move(response);
    return auth_status::complete;
}

} // namespace arknet::mysql::detail

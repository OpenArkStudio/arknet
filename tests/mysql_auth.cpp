#include <string>
#include <string_view>

#include "vendor/doctest/doctest.h"

#include <arknet/mysql/detail/auth.hpp>

namespace {

std::string hex(std::string_view bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    for (unsigned char byte : bytes)
    {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}

} // namespace

DOCTEST_TEST_CASE("MySQL authentication matches independent fixed SHA1 and SHA256 vectors") {
    using namespace arknet::mysql::detail;
    // Fixed vectors generated independently with hashlib from the documented
    // challenge-response formulas. No external program is needed to run this test.
    std::string response;
    REQUIRE(make_auth_response("mysql_native_password", "secret", "12345678901234567890", response) ==
            auth_status::complete);
    CHECK(hex(response) == "0f8b9033e0897c0a8338ebe3dea9010dda47ab56");
    REQUIRE(make_auth_response("caching_sha2_password", "secret", "12345678901234567890", response) ==
            auth_status::complete);
    CHECK(hex(response) == "51ecd6dedbd34d5445c0a190d4f51acf0d23b94db66c91f3f789faa9193751cd");
    const std::string password("a\0b\xff", 4);
    std::string challenge;
    for (int i = 0; i < 20; ++i)
    {
        challenge.push_back(static_cast<char>(i));
    }
    REQUIRE(make_auth_response("mysql_native_password", password, challenge, response) == auth_status::complete);
    CHECK(hex(response) == "af9321528cad4a479d3759deb16e35affb65354f");
    REQUIRE(make_auth_response("caching_sha2_password", password, challenge, response) == auth_status::complete);
    CHECK(hex(response) == "0ffb3f0bb85d233d8e642ad2b7ed0bc634be3003c63568dcb1bec3b83a267cd6");
}

DOCTEST_TEST_CASE("MySQL auth rejects unsupported mechanisms and malformed nonce without changing output") {
    using namespace arknet::mysql::detail;
    for (const auto plugin : {"mysql_native_password", "caching_sha2_password"})
    {
        for (const auto size : {0, 1, 19, 21, 1000})
        {
            std::string response = "previous";
            CHECK(make_auth_response(plugin, "secret", std::string(size, 'x'), response) ==
                  auth_status::invalid_challenge);
            CHECK(response == "previous");
        }
        std::string response = "previous";
        CHECK(make_auth_response(plugin, "", "12345678901234567890", response) == auth_status::complete);
        CHECK(response.empty());
    }
    for (const auto plugin : {"", "sha256_password", "mysql_clear_password", "unknown"})
    {
        std::string response = "previous";
        CHECK(
            make_auth_response(plugin, "secret", "12345678901234567890", response) == auth_status::unsupported_plugin);
        CHECK(response == "previous");
        CHECK(make_auth_response(plugin, "", "", response) == auth_status::unsupported_plugin);
    }
}

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>
#include <vector>

#include "vendor/fecpp/fecpp.h"

#include <arknet/thirdparty/fecpp.hpp>
#include "check.hpp"

namespace
{
using shards = std::vector<std::vector<std::uint8_t>>;

std::vector<std::uint8_t> data(std::size_t k, std::size_t size)
{
    std::vector<std::uint8_t> value(k * size);
    for (std::size_t i = 0; i < value.size(); ++i)
        value[i] = static_cast<std::uint8_t>((i * 131 + i / 7 + k * 17) & 255);
    return value;
}

template <class Code> shards encode(const Code& code, const std::vector<std::uint8_t>& value)
{
    shards result(code.get_N());
    code.encode(value.data(), value.size(),
                [&](std::size_t id, std::size_t n, const std::uint8_t* bytes, std::size_t size)
                {
                    check(n == code.get_N() && id < n, "encoder reports valid shard indices");
                    result[id].assign(bytes, bytes + size);
                });
    return result;
}

template <class Code> shards decode(const Code& code, const shards& encoded, unsigned survivors)
{
    std::map<std::size_t, const std::uint8_t*> selected;
    for (std::size_t id = 0; id < encoded.size(); ++id)
        if (survivors & (1U << id))
            selected.emplace(id, encoded[id].data());
    shards result(code.get_K());
    code.decode(selected, encoded.front().size(),
                [&](std::size_t id, std::size_t k, const std::uint8_t* bytes, std::size_t size)
                {
                    check(k == code.get_K() && id < k, "decoder reports valid primary indices");
                    result[id].assign(bytes, bytes + size);
                });
    return result;
}
}

DOCTEST_TEST_CASE("Reed-Solomon parity and recovery match the independent pinned fecpp reference")
{
    for (unsigned k = 1; k <= 6; ++k)
        for (unsigned p = 0; p <= 3; ++p)
            for (std::size_t size : {std::size_t(1), std::size_t(31), std::size_t(128)})
            {
                DOCTEST_CAPTURE(k);
                DOCTEST_CAPTURE(p);
                DOCTEST_CAPTURE(size);
                const auto input = data(k, size);
                arknet::detail::fecpp::fec_code adapted(k, k + p);
                fecpp::fec_code reference(k, k + p);
                const auto ours = encode(adapted, input);
                const auto upstream = encode(reference, input);
                check(ours == upstream, "all systematic and parity bytes match upstream");
                const shards expected(upstream.begin(), upstream.begin() + k);
                for (unsigned survivors = 0; survivors < (1U << (k + p)); ++survivors)
                {
                    if (std::popcount(survivors) < k)
                        continue;
                    DOCTEST_CAPTURE(survivors);
                    check(decode(adapted, upstream, survivors) == expected,
                          "adapted decoder repairs every recoverable upstream loss pattern");
                    check(decode(reference, ours, survivors) == expected,
                          "upstream decoder repairs every recoverable adapted loss pattern");
                }
            }
}

DOCTEST_TEST_CASE("Reed-Solomon rejects invalid counts, input sizes, and share maps")
{
    using arknet::detail::fecpp::fec_code;
    DOCTEST_CHECK_THROWS_AS(fec_code(0, 1), std::invalid_argument);
    DOCTEST_CHECK_THROWS_AS(fec_code(1, 0), std::invalid_argument);
    DOCTEST_CHECK_THROWS_AS(fec_code(3, 2), std::invalid_argument);
    DOCTEST_CHECK_THROWS_AS(fec_code(1, 257), std::invalid_argument);
    DOCTEST_CHECK_THROWS_AS(fec_code(std::numeric_limits<std::size_t>::max(), 4), std::invalid_argument);
    fec_code code(2, 4);
    const auto input = data(2, 8);
    const auto discard = [](std::size_t, std::size_t, const std::uint8_t*, std::size_t) {};
    DOCTEST_CHECK_THROWS_AS(code.encode(nullptr, input.size(), discard), std::invalid_argument);
    DOCTEST_CHECK_THROWS_AS(code.encode(input.data(), 0, discard), std::invalid_argument);
    DOCTEST_CHECK_THROWS_AS(code.encode(input.data(), 3, discard), std::invalid_argument);
    DOCTEST_CHECK_THROWS_AS(code.decode({{0, input.data()}}, 8, discard), std::logic_error);
    DOCTEST_CHECK_THROWS_AS(code.decode({{0, input.data()}, {4, input.data()}}, 8, discard), std::invalid_argument);
    DOCTEST_CHECK_THROWS_AS(code.decode({{0, input.data()}, {1, nullptr}}, 8, discard), std::invalid_argument);
    DOCTEST_CHECK_THROWS_AS(code.decode({{0, input.data()}, {1, input.data()}}, 0, discard), std::invalid_argument);
}

DOCTEST_TEST_CASE("Reed-Solomon maximum field size remains interoperable")
{
    const auto input = data(4, 5);
    arknet::detail::fecpp::fec_code adapted(4, 256);
    fecpp::fec_code reference(4, 256);
    check(encode(adapted, input) == encode(reference, input), "all 256 GF(256) share ids match upstream");
}

DOCTEST_TEST_CASE("Reed-Solomon instances can initialize and encode concurrently")
{
    std::atomic<bool> valid = true;
    std::vector<std::thread> workers;
    for (unsigned thread = 0; thread < 8; ++thread)
        workers.emplace_back(
            [&, thread]
            {
                for (unsigned round = 0; round < 32; ++round)
                {
                    arknet::detail::fecpp::fec_code code(4, 6);
                    const auto input = data(4, 16 + thread + round);
                    shards encoded(6);
                    code.encode(input.data(), input.size(),
                                [&](std::size_t id, std::size_t, const std::uint8_t* bytes, std::size_t count)
                                { encoded[id].assign(bytes, bytes + count); });
                    const std::map<std::size_t, const std::uint8_t*> selected = {
                        {1, encoded[1].data()}, {2, encoded[2].data()}, {4, encoded[4].data()}, {5, encoded[5].data()}};
                    code.decode(selected, input.size() / 4,
                                [&](std::size_t id, std::size_t, const std::uint8_t* bytes, std::size_t count)
                                {
                                    if (!std::equal(bytes, bytes + count,
                                                    input.begin() + static_cast<std::ptrdiff_t>(id * count)))
                                        valid = false;
                                });
                }
            });
    for (auto& worker : workers)
        worker.join();
    check(valid.load(), "concurrent codecs recover exact original bytes");
}

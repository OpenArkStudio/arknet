// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arknet/redis/client.hpp>
#include "check.hpp"

namespace redis_test {

using arknet::redis::client;
using arknet::redis::client_errc;
using arknet::redis::command;
using arknet::redis::result;
using arknet::redis::value_type;

class peer
{
public:
    explicit peer(asio::ip::tcp::socket& socket)
        : socket_(socket) {
        socket_.non_blocking(true);
    }

    command read_command() {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            arknet::redis::value reply;
            if (decoder_.next(reply) == arknet::redis::detail::resp_status::ready)
            {
                if (reply.type != value_type::array)
                {
                    throw std::runtime_error("Expected a RESP command array");
                }
                command arguments;
                for (auto& element : reply.elements)
                {
                    if (element.type != value_type::bulk_string)
                    {
                        throw std::runtime_error("Expected binary-safe command arguments");
                    }
                    arguments.push_back(std::move(element.text));
                }
                return arguments;
            }
            std::array<char, 4096> bytes;
            arknet::error_code error;
            auto count = socket_.read_some(asio::buffer(bytes), error);
            if (error == asio::error::would_block || error == asio::error::try_again)
            {
                std::this_thread::sleep_for(1ms);
                continue;
            }
            if (error)
            {
                throw arknet::system_error(error);
            }
            if (!decoder_.append(std::string_view(bytes.data(), count)))
            {
                throw std::runtime_error("Command decoder limit exceeded");
            }
        }
        throw std::runtime_error("Timed out reading a command");
    }

    void expect(command expected) {
        if (read_command() != expected)
        {
            throw std::runtime_error("Unexpected Redis command");
        }
    }

    void send(std::string_view data) {
        std::size_t position{};
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (position < data.size() && std::chrono::steady_clock::now() < deadline)
        {
            arknet::error_code error;
            position += socket_.write_some(asio::buffer(data.data() + position, data.size() - position), error);
            if (error == asio::error::would_block || error == asio::error::try_again)
            {
                std::this_thread::sleep_for(1ms);
            }
            else if (error)
            {
                throw arknet::system_error(error);
            }
        }
        if (position != data.size())
        {
            throw std::runtime_error("Timed out sending a response");
        }
    }

    void hello() {
        expect({"HELLO", "3"});
        send("%1\r\n+proto\r\n:3\r\n");
    }

    void wait_closed() {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        std::array<char, 64> bytes;
        while (std::chrono::steady_clock::now() < deadline)
        {
            arknet::error_code error;
            socket_.read_some(asio::buffer(bytes), error);
            if (error && error != asio::error::would_block && error != asio::error::try_again)
            {
                return;
            }
            std::this_thread::sleep_for(1ms);
        }
        throw std::runtime_error("Peer did not close the connection");
    }

private:
    asio::ip::tcp::socket& socket_;
    arknet::redis::detail::resp_decoder decoder_;
};

class server
{
public:
    template<class Function>
    explicit server(Function script, std::size_t connections = 1)
        : acceptor_(context_, {asio::ip::address_v4::loopback(), 0})
        , socket_(context_) {
        finished_ = completion_.get_future();
        acceptor_.non_blocking(true);
        worker_ = std::thread([this, script = std::move(script), connections]() mutable {
            try
            {
                for (std::size_t connection_index = 0; connection_index < connections; ++connection_index)
                {
                    const auto deadline = std::chrono::steady_clock::now() + 5s;
                    for (;;)
                    {
                        arknet::error_code error;
                        acceptor_.accept(socket_, error);
                        if (!error)
                        {
                            break;
                        }
                        if (error != asio::error::would_block && error != asio::error::try_again)
                        {
                            throw arknet::system_error(error);
                        }
                        if (std::chrono::steady_clock::now() >= deadline)
                        {
                            throw std::runtime_error("Timed out accepting Redis client");
                        }
                        std::this_thread::sleep_for(1ms);
                    }
                    peer connection(socket_);
                    script(connection);
                    socket_.close();
                }
                completion_.set_value();
            }
            catch (...)
            { completion_.set_exception(std::current_exception()); }
        });
    }

    ~server() {
        worker_.join();
    }

    std::string port() const {
        return std::to_string(acceptor_.local_endpoint().port());
    }

    void finish() {
        await(finished_);
    }

private:
    asio::io_context context_;
    asio::ip::tcp::acceptor acceptor_;
    asio::ip::tcp::socket socket_;
    std::promise<void> completion_;
    std::future<void> finished_;
    std::thread worker_;
};

template<class Client>
result connect(Client& connection, std::string port, arknet::redis::connect_options options = {}) {
    options.port = std::move(port);
    std::promise<result> completion;
    auto future = completion.get_future();
    connection.async_connect(
        std::move(options), [&completion](result response) { completion.set_value(std::move(response)); });
    return await(future);
}

} // namespace redis_test

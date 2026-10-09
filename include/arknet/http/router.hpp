// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <arknet/external/beast.hpp>
#include <arknet/base/detail/function.hpp>
#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arknet
{
using http_route_request = http::request<http::string_body>;
using http_route_response = http::response<http::string_body>;

struct http_route_context
{
    const http_route_request& request;
    std::string_view path;
    std::string_view query;
    std::vector<std::pair<std::string_view, std::string_view>> parameters;

    std::string_view param(std::string_view name) const noexcept
    {
        for (const auto& [key, value] : parameters)
            if (key == name)
                return value;
        return {};
    }
};

class http_router
{
    enum class segment_kind
    {
        wildcard,
        parameter,
        literal
    };
    struct segment
    {
        segment_kind kind;
        std::string value;
    };
    struct route
    {
        http::verb method;
        std::vector<segment> segments;
        detail::function<void(const http_route_context&, http_route_response&)> handler;
    };
    std::vector<route> routes_;
    std::vector<detail::function<bool(const http_route_context&, http_route_response&)>> middleware_;
    detail::function<void(std::exception_ptr)> error_handler_;

public:
    http_router() = default;
    http_router(const http_router&) = delete;
    http_router& operator=(const http_router&) = delete;
    http_router(http_router&&) = default;
    http_router& operator=(http_router&&) = default;

    template <class Handler> http_router& add(http::verb method, std::string_view pattern, Handler&& handler)
    {
        if (method == http::verb::unknown || http::to_string(method).empty())
            throw std::invalid_argument("HTTP route requires a known method");
        auto segments = parse_pattern(pattern);
        for (const auto& existing : routes_)
            if (existing.method == method && same_resource(existing.segments, segments))
                throw std::invalid_argument("duplicate HTTP method and route pattern");
        routes_.push_back({method, std::move(segments), std::forward<Handler>(handler)});
        return *this;
    }

    template <class Middleware> http_router& use(Middleware&& middleware)
    {
        middleware_.emplace_back(std::forward<Middleware>(middleware));
        return *this;
    }

    template <class Handler> http_router& bind_error(Handler&& handler)
    {
        error_handler_ = std::forward<Handler>(handler);
        return *this;
    }

    http_route_response operator()(const http_route_request& request) const
    {
        http_route_response response(http::status::ok, request.version());
        response.keep_alive(request.keep_alive());
        const std::string_view target(request.target().data(), request.target().size());
        std::string path;
        std::string_view query;
        if (!parse_target(target, request.method(), path, query))
        {
            response.result(http::status::bad_request);
            response.body() = "Bad Request";
            return finish(request, std::move(response));
        }
        const auto parts = split_path(path);
        const route* resource = nullptr;
        for (const auto& candidate : routes_)
            if (matches(candidate.segments, parts) &&
                (!resource || more_specific(candidate.segments, resource->segments)))
                resource = &candidate;

        const route* selected = nullptr;
        std::vector<http::verb> allowed;
        for (const auto& candidate : routes_)
        {
            if (path != "*" && (!resource || !same_resource(candidate.segments, resource->segments)))
                continue;
            allowed.push_back(candidate.method);
            if (candidate.method == request.method())
                selected = &candidate;
        }
        if (request.method() == http::verb::head && !selected && resource)
            for (const auto& candidate : routes_)
                if (candidate.method == http::verb::get && same_resource(candidate.segments, resource->segments))
                    selected = &candidate;
        const std::string allow = make_allow(std::move(allowed));
        http_route_context context{request, path, query, {}};
        if (selected && path != "*")
            capture(*selected, parts, path, context);

        try
        {
            bool proceed = true;
            for (const auto& middleware : middleware_)
                if (!middleware(context, response))
                {
                    proceed = false;
                    break;
                }
            if (proceed)
            {
                if (path == "*" || (resource && request.method() == http::verb::options && !selected))
                {
                    response.result(http::status::no_content);
                    response.set(http::field::allow, allow);
                }
                else if (!resource)
                {
                    response.result(http::status::not_found);
                    response.body() = "Not Found";
                }
                else if (!selected)
                {
                    response.result(http::status::method_not_allowed);
                    response.set(http::field::allow, allow);
                    response.body() = "Method Not Allowed";
                }
                else
                    selected->handler(context, response);
            }
            return finish(request, std::move(response));
        }
        catch (...)
        {
            const auto exception = std::current_exception();
            if (error_handler_)
            {
                try
                {
                    error_handler_(exception);
                }
                catch (...)
                {
                }
            }
            http_route_response failure(http::status::internal_server_error, request.version());
            failure.keep_alive(request.keep_alive());
            failure.body() = "Internal Server Error";
            return finish(request, std::move(failure));
        }
    }

private:
    static bool name_valid(std::string_view name) noexcept
    {
        if (name.empty())
            return false;
        const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
        if (!letter(name.front()))
            return false;
        return std::all_of(name.begin(), name.end(), [&](char c) { return letter(c) || (c >= '0' && c <= '9'); });
    }

    static int hex(char c) noexcept
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    static bool uri_character(unsigned char c) noexcept
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               std::string_view("-._~!$&'()*+,;=:@/").find(static_cast<char>(c)) != std::string_view::npos;
    }

    static bool decode(std::string_view input, std::string* output, bool query = false)
    {
        for (std::size_t index = 0; index < input.size(); ++index)
        {
            auto value = static_cast<unsigned char>(input[index]);
            if (value == '%')
            {
                if (index + 2 >= input.size())
                    return false;
                const int high = hex(input[index + 1]), low = hex(input[index + 2]);
                if (high < 0 || low < 0)
                    return false;
                value = static_cast<unsigned char>((high << 4) | low);
                index += 2;
                if (value < 0x20 || value == 0x7f || (!query && (value == '/' || value == '\\')))
                    return false;
            }
            else if (!uri_character(value) && !(query && value == '?'))
                return false;
            if (output)
                output->push_back(static_cast<char>(value));
        }
        return true;
    }

    static std::vector<std::string_view> split_path(std::string_view path)
    {
        std::vector<std::string_view> result;
        if (path == "/" || path == "*")
            return result;
        path.remove_prefix(1);
        while (true)
        {
            const auto slash = path.find('/');
            result.push_back(path.substr(0, slash));
            if (slash == std::string_view::npos)
                return result;
            path.remove_prefix(slash + 1);
        }
    }

    static bool parse_target(std::string_view target, http::verb method, std::string& path, std::string_view& query)
    {
        if (target == "*" && method == http::verb::options)
        {
            path = "*";
            return true;
        }
        if (target.empty() || target.front() != '/')
            return false;
        const auto separator = target.find('?');
        if (!decode(target.substr(0, separator), &path))
            return false;
        if (separator != std::string_view::npos)
        {
            query = target.substr(separator + 1);
            if (!decode(query, nullptr, true))
                return false;
        }
        for (auto part : split_path(path))
            if (part == "." || part == "..")
                return false;
        return true;
    }

    static std::vector<segment> parse_pattern(std::string_view pattern)
    {
        if (pattern.empty() || pattern.front() != '/' || pattern.find_first_of("?#") != std::string_view::npos)
            throw std::invalid_argument("HTTP route patterns require an absolute path without query or fragment");
        const auto parts = split_path(pattern);
        std::vector<segment> result;
        std::vector<std::string_view> names;
        for (std::size_t index = 0; index < parts.size(); ++index)
        {
            const auto part = parts[index];
            if (!part.empty() && (part.front() == ':' || part.front() == '*'))
            {
                const auto name = part.substr(1);
                if (!name_valid(name) || std::find(names.begin(), names.end(), name) != names.end() ||
                    (part.front() == '*' && index + 1 != parts.size()))
                    throw std::invalid_argument("HTTP parameter names must be unique; wildcards must end the path");
                names.push_back(name);
                result.push_back(
                    {part.front() == ':' ? segment_kind::parameter : segment_kind::wildcard, std::string(name)});
            }
            else
            {
                std::string literal;
                if (!decode(part, &literal) || literal == "." || literal == "..")
                    throw std::invalid_argument("invalid HTTP route path segment");
                result.push_back({segment_kind::literal, std::move(literal)});
            }
        }
        return result;
    }

    static bool same_resource(const std::vector<segment>& left, const std::vector<segment>& right) noexcept
    {
        if (left.size() != right.size())
            return false;
        for (std::size_t index = 0; index < left.size(); ++index)
            if (left[index].kind != right[index].kind ||
                (left[index].kind == segment_kind::literal && left[index].value != right[index].value))
                return false;
        return true;
    }

    static bool matches(const std::vector<segment>& pattern, const std::vector<std::string_view>& parts) noexcept
    {
        for (std::size_t index = 0; index < pattern.size(); ++index)
        {
            if (pattern[index].kind == segment_kind::wildcard)
                return true;
            if (index >= parts.size())
                return false;
            if (pattern[index].kind == segment_kind::literal && pattern[index].value != parts[index])
                return false;
            if (pattern[index].kind == segment_kind::parameter && parts[index].empty())
                return false;
        }
        return pattern.size() == parts.size();
    }

    static bool more_specific(const std::vector<segment>& left, const std::vector<segment>& right) noexcept
    {
        for (std::size_t index = 0; index < (std::min)(left.size(), right.size()); ++index)
            if (left[index].kind != right[index].kind)
                return left[index].kind > right[index].kind;
        // An exact path also wins over a wildcard that captures no segments.
        return left.size() < right.size();
    }

    static void capture(const route& selected, const std::vector<std::string_view>& parts, std::string_view path,
                        http_route_context& context)
    {
        for (std::size_t index = 0; index < selected.segments.size(); ++index)
        {
            const auto& part = selected.segments[index];
            if (part.kind == segment_kind::parameter)
                context.parameters.emplace_back(part.value, parts[index]);
            else if (part.kind == segment_kind::wildcard)
            {
                const auto value = index < parts.size()
                                       ? path.substr(static_cast<std::size_t>(parts[index].data() - path.data()))
                                       : std::string_view{};
                context.parameters.emplace_back(part.value, value);
            }
        }
    }

    static std::string make_allow(std::vector<http::verb> methods)
    {
        if (std::find(methods.begin(), methods.end(), http::verb::get) != methods.end())
            methods.push_back(http::verb::head);
        methods.push_back(http::verb::options);
        std::sort(methods.begin(), methods.end(),
                  [](auto left, auto right) { return http::to_string(left) < http::to_string(right); });
        methods.erase(std::unique(methods.begin(), methods.end()), methods.end());
        std::string result;
        for (auto method : methods)
        {
            if (!result.empty())
                result += ", ";
            const auto name = http::to_string(method);
            result.append(name.data(), name.size());
        }
        return result;
    }

    static http_route_response finish(const http_route_request& request, http_route_response response)
    {
        response.version(request.version());
        const auto status = response.result_int();
        if (status < 200 || status == 204 || status == 205 || status == 304)
        {
            response.body().clear();
            response.erase(http::field::transfer_encoding);
            if (status != 304)
                response.erase(http::field::content_length);
            if (status == 205)
                response.content_length(0);
        }
        else if (request.method() == http::verb::head)
        {
            if (!response.count(http::field::content_length))
                response.content_length(response.body().size());
            response.body().clear();
            response.erase(http::field::transfer_encoding);
        }
        else
            response.prepare_payload();
        return response;
    }
};

template <class Server> Server& bind_router(Server& server, std::shared_ptr<const http_router> router)
{
    if (!router)
        throw std::invalid_argument("null HTTP router");
    server.bind_recv([router = std::move(router)](auto& session, http_route_request& request)
                     { session->async_send((*router)(request)); });
    return server;
}
}

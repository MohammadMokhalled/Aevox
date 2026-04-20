#pragma once
// include/aevox/router_impl.hpp
//
// Template method body definitions for Router and App.
// Included at the bottom of router.hpp (for Router methods) and at the
// bottom of app.hpp (for App forwarding methods).
//
// This file includes src/router/handler_wrap.hpp which provides the
// normalise_handler template and the parse_pattern / extract_param_* helpers.
//
// IMPORTANT: This file is part of the public include tree (under include/aevox/)
// but must never be included directly by application code. It is only reachable
// via router.hpp / app.hpp. The ${CMAKE_SOURCE_DIR}/src directory must be on
// the include path of any translation unit that includes router.hpp or app.hpp.
// All in-tree consumers (src/, tests/) satisfy this requirement.
//
// Constraint: src/router/handler_wrap.hpp must contain ONLY standard-library
// headers, aevox public headers, and no Asio/third-party types. See OI-1 in
// Tasks/architecture/AEV-004-arch.md §10.
//
// Design: Tasks/architecture/AEV-004-arch.md §4.3

#include <span>
#include <string>
#include <utility>

#include "router/handler_wrap.hpp" // resolved via ${CMAKE_SOURCE_DIR}/src

// =============================================================================
// Router template method bodies (out-of-class definitions)
// =============================================================================

template <typename Handler> void aevox::Router::get(std::string_view pattern, Handler&& handler)
{
    auto segs  = aevox::detail::parse_pattern(pattern);
    auto names = aevox::detail::extract_param_names(segs);
    auto types = aevox::detail::extract_param_types(segs);
    register_route(aevox::HttpMethod::GET, std::span{segs},
                   aevox::detail::normalise_handler(std::forward<Handler>(handler),
                                                    std::span{names}, std::span{types}));
}

template <typename Handler> void aevox::Router::post(std::string_view pattern, Handler&& handler)
{
    auto segs  = aevox::detail::parse_pattern(pattern);
    auto names = aevox::detail::extract_param_names(segs);
    auto types = aevox::detail::extract_param_types(segs);
    register_route(aevox::HttpMethod::POST, std::span{segs},
                   aevox::detail::normalise_handler(std::forward<Handler>(handler),
                                                    std::span{names}, std::span{types}));
}

template <typename Handler> void aevox::Router::put(std::string_view pattern, Handler&& handler)
{
    auto segs  = aevox::detail::parse_pattern(pattern);
    auto names = aevox::detail::extract_param_names(segs);
    auto types = aevox::detail::extract_param_types(segs);
    register_route(aevox::HttpMethod::PUT, std::span{segs},
                   aevox::detail::normalise_handler(std::forward<Handler>(handler),
                                                    std::span{names}, std::span{types}));
}

template <typename Handler> void aevox::Router::patch(std::string_view pattern, Handler&& handler)
{
    auto segs  = aevox::detail::parse_pattern(pattern);
    auto names = aevox::detail::extract_param_names(segs);
    auto types = aevox::detail::extract_param_types(segs);
    register_route(aevox::HttpMethod::PATCH, std::span{segs},
                   aevox::detail::normalise_handler(std::forward<Handler>(handler),
                                                    std::span{names}, std::span{types}));
}

template <typename Handler> void aevox::Router::del(std::string_view pattern, Handler&& handler)
{
    auto segs  = aevox::detail::parse_pattern(pattern);
    auto names = aevox::detail::extract_param_names(segs);
    auto types = aevox::detail::extract_param_types(segs);
    register_route(aevox::HttpMethod::DELETE, std::span{segs},
                   aevox::detail::normalise_handler(std::forward<Handler>(handler),
                                                    std::span{names}, std::span{types}));
}

template <typename Handler> void aevox::Router::options(std::string_view pattern, Handler&& handler)
{
    auto segs  = aevox::detail::parse_pattern(pattern);
    auto names = aevox::detail::extract_param_names(segs);
    auto types = aevox::detail::extract_param_types(segs);
    register_route(aevox::HttpMethod::OPTIONS, std::span{segs},
                   aevox::detail::normalise_handler(std::forward<Handler>(handler),
                                                    std::span{names}, std::span{types}));
}

// App template method bodies are defined inline in include/aevox/app.hpp
// (after the App class definition and AFTER router.hpp is included, so that
// App is a complete type). Putting them here would fail because router.hpp
// includes this file before app.hpp has a chance to declare App.

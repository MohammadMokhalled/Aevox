#pragma once
// src/router/handler_wrap.hpp
//
// INTERNAL — never included outside src/router/ or include/aevox/router_impl.hpp.
//
// Defines ErasedHandler, ParamType, Segment, and the normalise_handler<Handler>
// function template. Also declares (but does NOT define) parse_pattern,
// extract_param_names, and extract_param_types — their definitions live in
// router_impl.cpp.
//
// Permitted includes: ONLY standard library headers plus aevox public headers.
// No Asio types. No third-party library types. This constraint is required by
// CLAUDE.md §3.1/§3.2: router_impl.hpp is indirectly included from the public
// header include/aevox/router.hpp (via router_impl.hpp). Any type appearing here
// becomes transitively visible to application code.
//
// Design: AEV-004-arch.md §4.3

#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/task.hpp>

#include <concepts>
#include <format>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace aevox::detail {

// =============================================================================
// ErasedHandler
// =============================================================================

/// Type-erased handler stored in every trie node method slot.
/// All handler variants are normalised to this signature at registration time.
/// std::function requires CopyConstructible captures: lambdas that capture
/// move-only types (e.g. std::unique_ptr) must wrap them in std::shared_ptr.
/// Upgrade to std::move_only_function is tracked in AEV-015.
using ErasedHandler = std::function<aevox::Task<aevox::Response>(aevox::Request&)>;

// =============================================================================
// ParamType
// =============================================================================

/// Type annotation for a named route parameter.
/// Used to record the type suffix from the pattern (e.g. `{id:int}` → Int).
enum class ParamType : std::uint8_t
{
    None,   ///< No type annotation; raw string.
    Int,    ///< :int  — extracted as int via std::from_chars.
    UInt,   ///< :uint — extracted as unsigned int via std::from_chars.
    Float,  ///< :float  — extracted as float via std::from_chars.
    Double, ///< :double — extracted as double via std::from_chars.
    String, ///< :string (or no annotation) — extracted as std::string.
};

// =============================================================================
// Segment
// =============================================================================

/// One parsed segment of a route pattern.
struct Segment
{
    enum class Kind : std::uint8_t
    {
        Static,   ///< Literal text match (e.g. "users").
        Param,    ///< Named capture ({id} or {id:int}).
        Wildcard, ///< Greedy tail capture ({path...}). Must be last segment.
    };

    Kind       kind{Kind::Static};
    std::string name;    ///< Param or wildcard name (empty for Static).
    std::string literal; ///< Literal text (empty for Param/Wildcard).
    ParamType   param_type{ParamType::None};
};

// =============================================================================
// Pattern helpers — declared here, defined in router_impl.cpp
// =============================================================================

/// Parses a route pattern string into an ordered segment vector.
/// Calls std::terminate on malformed input (unclosed '{', unknown type suffix,
/// wildcard not at tail). Route patterns are programmer constants — a
/// malformed pattern is a startup-time defect, not a recoverable error.
std::vector<Segment> parse_pattern(std::string_view pattern);

/// Extracts param/wildcard names from a segment vector, in left-to-right order.
std::vector<std::string> extract_param_names(const std::vector<Segment>& segs);

/// Extracts param types from a segment vector, in left-to-right order.
std::vector<ParamType>   extract_param_types(const std::vector<Segment>& segs);

// =============================================================================
// Internal concepts (used only by normalise_handler)
// =============================================================================

template <typename H>
concept SyncHandlerBase =
    std::invocable<H, aevox::Request&> &&
    std::same_as<std::invoke_result_t<H, aevox::Request&>, aevox::Response>;

template <typename H>
concept AsyncHandlerBase =
    std::invocable<H, aevox::Request&> &&
    std::same_as<std::invoke_result_t<H, aevox::Request&>, aevox::Task<aevox::Response>>;

// =============================================================================
// normalise_handler — converts any supported handler variant to ErasedHandler
// =============================================================================

/// Normalises any supported handler callable into a uniform ErasedHandler.
///
/// Supported signatures (T0, T1 each from {int, unsigned int, float, double,
/// std::string}):
///   (Request&) -> Response
///   (Request&) -> Task<Response>
///   (Request&, T0) -> Response
///   (Request&, T0) -> Task<Response>
///   (Request&, T0, T1) -> Response
///   (Request&, T0, T1) -> Task<Response>
///
/// param_names: names of pattern parameters, left-to-right.
/// param_types: type annotations (informational; extraction uses T0/T1 from handler).
///
/// @note std::function requires CopyConstructible captures. Handlers that capture
///       move-only types must wrap them in std::shared_ptr (OI-2, AEV-015).
template <typename Handler>
ErasedHandler normalise_handler(Handler&&                        h,
                                std::span<const std::string>     param_names,
                                std::span<const ParamType>       /*param_types*/)
{
    // Helper: wrap a sync call into a coroutine.
    // Used below for sync handler variants.

    // -------------------------------------------------------------------------
    // Arity 0: (Request&) -> Task<Response>
    // -------------------------------------------------------------------------
    if constexpr (AsyncHandlerBase<Handler>) {
        return ErasedHandler{std::forward<Handler>(h)};
    }

    // -------------------------------------------------------------------------
    // Arity 0: (Request&) -> Response
    // -------------------------------------------------------------------------
    else if constexpr (SyncHandlerBase<Handler>) {
        return [h = std::forward<Handler>(h)](aevox::Request& r)
                   -> aevox::Task<aevox::Response> { co_return h(r); };
    }

    // -------------------------------------------------------------------------
    // Arity 1 — int (checked before unsigned to avoid ambiguity from implicit
    //           narrowing conversions; both int and unsigned match unsigned,
    //           but int handler is more specific)
    // -------------------------------------------------------------------------
    else if constexpr (std::is_invocable_v<Handler, aevox::Request&, int>) {
        std::string name0{param_names.empty() ? "" : param_names[0]};
        return [h = std::forward<Handler>(h), name0](aevox::Request& r)
                   -> aevox::Task<aevox::Response> {
            auto v = r.param<int>(name0);
            if (!v) {
                co_return aevox::Response::bad_request(
                    std::format("bad param '{}': expected int", name0));
            }
            if constexpr (std::is_same_v<std::invoke_result_t<Handler, aevox::Request&, int>,
                                         aevox::Task<aevox::Response>>) {
                co_return co_await h(r, *v);
            }
            else {
                co_return h(r, *v);
            }
        };
    }

    // -------------------------------------------------------------------------
    // Arity 1 — unsigned int
    // -------------------------------------------------------------------------
    else if constexpr (std::is_invocable_v<Handler, aevox::Request&, unsigned int>) {
        std::string name0{param_names.empty() ? "" : param_names[0]};
        return [h = std::forward<Handler>(h), name0](aevox::Request& r)
                   -> aevox::Task<aevox::Response> {
            auto v = r.param<unsigned int>(name0);
            if (!v) {
                co_return aevox::Response::bad_request(
                    std::format("bad param '{}': expected uint", name0));
            }
            if constexpr (std::is_same_v<
                              std::invoke_result_t<Handler, aevox::Request&, unsigned int>,
                              aevox::Task<aevox::Response>>) {
                co_return co_await h(r, *v);
            }
            else {
                co_return h(r, *v);
            }
        };
    }

    // -------------------------------------------------------------------------
    // Arity 1 — float
    // -------------------------------------------------------------------------
    else if constexpr (std::is_invocable_v<Handler, aevox::Request&, float>) {
        std::string name0{param_names.empty() ? "" : param_names[0]};
        return [h = std::forward<Handler>(h), name0](aevox::Request& r)
                   -> aevox::Task<aevox::Response> {
            auto v = r.param<float>(name0);
            if (!v) {
                co_return aevox::Response::bad_request(
                    std::format("bad param '{}': expected float", name0));
            }
            if constexpr (std::is_same_v<std::invoke_result_t<Handler, aevox::Request&, float>,
                                         aevox::Task<aevox::Response>>) {
                co_return co_await h(r, *v);
            }
            else {
                co_return h(r, *v);
            }
        };
    }

    // -------------------------------------------------------------------------
    // Arity 1 — double
    // -------------------------------------------------------------------------
    else if constexpr (std::is_invocable_v<Handler, aevox::Request&, double>) {
        std::string name0{param_names.empty() ? "" : param_names[0]};
        return [h = std::forward<Handler>(h), name0](aevox::Request& r)
                   -> aevox::Task<aevox::Response> {
            auto v = r.param<double>(name0);
            if (!v) {
                co_return aevox::Response::bad_request(
                    std::format("bad param '{}': expected double", name0));
            }
            if constexpr (std::is_same_v<std::invoke_result_t<Handler, aevox::Request&, double>,
                                         aevox::Task<aevox::Response>>) {
                co_return co_await h(r, *v);
            }
            else {
                co_return h(r, *v);
            }
        };
    }

    // -------------------------------------------------------------------------
    // Arity 1 — std::string
    // -------------------------------------------------------------------------
    else if constexpr (std::is_invocable_v<Handler, aevox::Request&, std::string>) {
        std::string name0{param_names.empty() ? "" : param_names[0]};
        return [h = std::forward<Handler>(h), name0](aevox::Request& r)
                   -> aevox::Task<aevox::Response> {
            auto v = r.param<std::string>(name0);
            if (!v) {
                co_return aevox::Response::bad_request(
                    std::format("bad param '{}': not found", name0));
            }
            if constexpr (std::is_same_v<
                              std::invoke_result_t<Handler, aevox::Request&, std::string>,
                              aevox::Task<aevox::Response>>) {
                co_return co_await h(r, *v);
            }
            else {
                co_return h(r, *v);
            }
        };
    }

    // -------------------------------------------------------------------------
    // Arity 2 — (std::string, int)
    // -------------------------------------------------------------------------
    else if constexpr (std::is_invocable_v<Handler, aevox::Request&, std::string, int>) {
        std::string name0{param_names.size() > 0 ? param_names[0] : ""};
        std::string name1{param_names.size() > 1 ? param_names[1] : ""};
        return [h = std::forward<Handler>(h), name0, name1](aevox::Request& r)
                   -> aevox::Task<aevox::Response> {
            auto v0 = r.param<std::string>(name0);
            auto v1 = r.param<int>(name1);
            if (!v0) {
                co_return aevox::Response::bad_request(
                    std::format("bad param '{}'", name0));
            }
            if (!v1) {
                co_return aevox::Response::bad_request(
                    std::format("bad param '{}': expected int", name1));
            }
            if constexpr (std::is_same_v<
                              std::invoke_result_t<Handler, aevox::Request&, std::string, int>,
                              aevox::Task<aevox::Response>>) {
                co_return co_await h(r, *v0, *v1);
            }
            else {
                co_return h(r, *v0, *v1);
            }
        };
    }

    // -------------------------------------------------------------------------
    // Arity 2 — (int, std::string)
    // -------------------------------------------------------------------------
    else if constexpr (std::is_invocable_v<Handler, aevox::Request&, int, std::string>) {
        std::string name0{param_names.size() > 0 ? param_names[0] : ""};
        std::string name1{param_names.size() > 1 ? param_names[1] : ""};
        return [h = std::forward<Handler>(h), name0, name1](aevox::Request& r)
                   -> aevox::Task<aevox::Response> {
            auto v0 = r.param<int>(name0);
            auto v1 = r.param<std::string>(name1);
            if (!v0) {
                co_return aevox::Response::bad_request(
                    std::format("bad param '{}': expected int", name0));
            }
            if (!v1) {
                co_return aevox::Response::bad_request(
                    std::format("bad param '{}'", name1));
            }
            if constexpr (std::is_same_v<
                              std::invoke_result_t<Handler, aevox::Request&, int, std::string>,
                              aevox::Task<aevox::Response>>) {
                co_return co_await h(r, *v0, *v1);
            }
            else {
                co_return h(r, *v0, *v1);
            }
        };
    }

    // -------------------------------------------------------------------------
    // Arity 2 — (int, int)
    // -------------------------------------------------------------------------
    else if constexpr (std::is_invocable_v<Handler, aevox::Request&, int, int>) {
        std::string name0{param_names.size() > 0 ? param_names[0] : ""};
        std::string name1{param_names.size() > 1 ? param_names[1] : ""};
        return [h = std::forward<Handler>(h), name0, name1](aevox::Request& r)
                   -> aevox::Task<aevox::Response> {
            auto v0 = r.param<int>(name0);
            auto v1 = r.param<int>(name1);
            if (!v0) {
                co_return aevox::Response::bad_request(
                    std::format("bad param '{}': expected int", name0));
            }
            if (!v1) {
                co_return aevox::Response::bad_request(
                    std::format("bad param '{}': expected int", name1));
            }
            if constexpr (std::is_same_v<
                              std::invoke_result_t<Handler, aevox::Request&, int, int>,
                              aevox::Task<aevox::Response>>) {
                co_return co_await h(r, *v0, *v1);
            }
            else {
                co_return h(r, *v0, *v1);
            }
        };
    }

    // -------------------------------------------------------------------------
    // Arity 2 — (std::string, std::string)
    // -------------------------------------------------------------------------
    else if constexpr (std::is_invocable_v<Handler, aevox::Request&, std::string, std::string>) {
        std::string name0{param_names.size() > 0 ? param_names[0] : ""};
        std::string name1{param_names.size() > 1 ? param_names[1] : ""};
        return [h = std::forward<Handler>(h), name0, name1](aevox::Request& r)
                   -> aevox::Task<aevox::Response> {
            auto v0 = r.param<std::string>(name0);
            auto v1 = r.param<std::string>(name1);
            if (!v0) {
                co_return aevox::Response::bad_request(
                    std::format("bad param '{}'", name0));
            }
            if (!v1) {
                co_return aevox::Response::bad_request(
                    std::format("bad param '{}'", name1));
            }
            if constexpr (std::is_same_v<
                              std::invoke_result_t<Handler, aevox::Request&, std::string,
                                                   std::string>,
                              aevox::Task<aevox::Response>>) {
                co_return co_await h(r, *v0, *v1);
            }
            else {
                co_return h(r, *v0, *v1);
            }
        };
    }

    else {
        // If none of the above branches matched, the handler signature is unsupported.
        // This static_assert fires at compile time with a readable diagnostic.
        static_assert(!std::is_same_v<Handler, Handler>,
                      "Unsupported handler signature. Supported forms:\n"
                      "  (Request&) -> Response\n"
                      "  (Request&) -> Task<Response>\n"
                      "  (Request&, T) -> Response      -- T in {int,uint,float,double,string}\n"
                      "  (Request&, T) -> Task<Response>\n"
                      "  (Request&, T0, T1) -> Response/Task<Response>\n");
        return {};
    }
}

} // namespace aevox::detail

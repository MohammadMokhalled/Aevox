// src/router/router_impl.cpp
//
// INTERNAL — implements the radix trie Router.
//
// Provides:
//   parse_pattern, extract_param_names, extract_param_types
//   Router::Impl::insert, Router::Impl::ensure_child
//   Router ctor / dtor / move / valid()
//   Router::register_route
//   Router::dispatch  (coroutine)
//   Router::group
//
// Dependency note: includes request_impl.hpp to gain friend-class access to
// Request::Impl::params for path-parameter injection in dispatch().
//
// Design: AEV-004-arch.md §6, §7

#include "router/router_impl.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "http/request_impl.hpp"

namespace aevox {

// =============================================================================
// Anonymous-namespace helpers
// =============================================================================

namespace {

/// Method names for Allow header construction. Index = static_cast<uint8_t>(HttpMethod).
constexpr std::array<std::string_view, kMethodCount> kMethodNames{"GET",    "POST",   "PUT",
                                                                  "PATCH",  "DELETE", "HEAD",
                                                                  "OPTIONS"};

/// Builds the comma-separated Allow header value from a method_mask bitmask.
std::string build_allow_header(std::uint8_t method_mask)
{
    std::string result;
    for (std::size_t i = 0; i < kMethodCount; ++i) {
        if (method_mask & (static_cast<std::uint8_t>(1u) << i)) {
            if (!result.empty())
                result += ", ";
            result += kMethodNames[i];
        }
    }
    return result;
}

} // namespace

// =============================================================================
// parse_pattern — declared in handler_wrap.hpp, defined here
// =============================================================================

namespace detail {

std::vector<Segment> parse_pattern(std::string_view pattern)
{
    if (pattern.empty() || pattern[0] != '/') {
        // Route patterns must start with '/'.
        std::terminate();
    }

    std::vector<Segment> segs;
    std::size_t          pos          = 1; // skip leading '/'
    bool                 had_wildcard = false;

    while (pos <= pattern.size()) {
        const auto end = [&]() -> std::size_t {
            auto p = pattern.find('/', pos);
            return (p == std::string_view::npos) ? pattern.size() : p;
        }();

        const std::string_view tok = pattern.substr(pos, end - pos);

        if (!tok.empty()) {
            if (had_wildcard) {
                // Wildcard must be the last segment.
                std::terminate();
            }

            Segment seg;

            if (tok.size() >= 2 && tok.front() == '{' && tok.back() == '}') {
                const std::string_view inner = tok.substr(1, tok.size() - 2);

                if (inner.ends_with("...")) {
                    // Wildcard: {name...}
                    seg.kind = Segment::Kind::Wildcard;
                    seg.name = std::string{inner.substr(0, inner.size() - 3)};
                    if (seg.name.empty())
                        std::terminate(); // nameless wildcard
                    had_wildcard = true;
                }
                else {
                    // Named parameter: {name} or {name:type}
                    seg.kind             = Segment::Kind::Param;
                    const auto colon_pos = inner.find(':');
                    if (colon_pos == std::string_view::npos) {
                        seg.name       = std::string{inner};
                        seg.param_type = ParamType::None;
                    }
                    else {
                        seg.name                       = std::string{inner.substr(0, colon_pos)};
                        const std::string_view type_sv = inner.substr(colon_pos + 1);
                        if (type_sv == "int")
                            seg.param_type = ParamType::Int;
                        else if (type_sv == "uint")
                            seg.param_type = ParamType::UInt;
                        else if (type_sv == "float")
                            seg.param_type = ParamType::Float;
                        else if (type_sv == "double")
                            seg.param_type = ParamType::Double;
                        else if (type_sv == "string")
                            seg.param_type = ParamType::String;
                        else
                            std::terminate(); // unknown type suffix
                    }
                    if (seg.name.empty())
                        std::terminate(); // nameless param
                }
            }
            else {
                // Static segment — sanity check for stray braces
                if (tok.find('{') != std::string_view::npos ||
                    tok.find('}') != std::string_view::npos)
                {
                    std::terminate(); // unclosed or misplaced brace
                }
                seg.kind    = Segment::Kind::Static;
                seg.literal = std::string{tok};
            }

            segs.push_back(std::move(seg));
        }

        if (end == pattern.size())
            break;
        pos = end + 1;
    }

    return segs;
}

std::vector<std::string> extract_param_names(const std::vector<Segment>& segs)
{
    std::vector<std::string> names;
    for (const auto& s : segs) {
        if (s.kind != Segment::Kind::Static)
            names.push_back(s.name);
    }
    return names;
}

std::vector<ParamType> extract_param_types(const std::vector<Segment>& segs)
{
    std::vector<ParamType> types;
    for (const auto& s : segs) {
        if (s.kind != Segment::Kind::Static)
            types.push_back(s.param_type);
    }
    return types;
}

} // namespace detail

// =============================================================================
// Router::Impl — trie insertion helpers
// =============================================================================

TrieNode* Router::Impl::ensure_child(TrieNode* node, const detail::Segment& seg)
{
    using Kind = detail::Segment::Kind;

    if (seg.kind == Kind::Static) {
        for (auto& child : node->static_children) {
            if (child->segment == seg.literal)
                return child.get();
        }
        auto child     = std::make_unique<TrieNode>();
        child->kind    = TrieNode::NodeKind::Static;
        child->segment = seg.literal;
        auto* ptr      = child.get();
        node->static_children.push_back(std::move(child));
        return ptr;
    }

    if (seg.kind == Kind::Param) {
        if (!node->param_child) {
            node->param_child             = std::make_unique<TrieNode>();
            node->param_child->kind       = TrieNode::NodeKind::Param;
            node->param_child->segment    = seg.name;
            node->param_child->param_type = seg.param_type;
        }
        return node->param_child.get();
    }

    // Wildcard
    if (!node->wildcard_child) {
        node->wildcard_child          = std::make_unique<TrieNode>();
        node->wildcard_child->kind    = TrieNode::NodeKind::Wildcard;
        node->wildcard_child->segment = seg.name;
    }
    return node->wildcard_child.get();
}

void Router::Impl::insert(TrieNode* node, std::span<const detail::Segment> segs, HttpMethod method,
                          detail::ErasedHandler handler)
{
    if (segs.empty()) {
        const auto idx      = static_cast<std::size_t>(static_cast<std::uint8_t>(method));
        node->handlers[idx] = std::move(handler);
        node->method_mask |= static_cast<std::uint8_t>(1u << idx);
        return;
    }
    TrieNode* child = ensure_child(node, segs[0]);
    insert(child, segs.subspan(1), method, std::move(handler));
}

// =============================================================================
// Router — constructors / destructor / move
// =============================================================================

Router::Router()
{
    auto impl          = std::make_unique<Impl>();
    impl->root_        = std::make_unique<TrieNode>();
    impl->insert_root_ = impl->root_.get();
    impl->owns_root_   = true;
    impl_              = std::move(impl);
}

Router::~Router() = default;

Router::Router(Router&& other) noexcept = default;

Router& Router::operator=(Router&& other) noexcept = default;

Router::Router(GroupTag, std::unique_ptr<Impl> group_impl) noexcept : impl_{std::move(group_impl)}
{}

// =============================================================================
// Router — valid()
// =============================================================================

bool Router::valid() const noexcept
{
    return impl_ != nullptr;
}

// =============================================================================
// Router::register_route — private, called by template methods in router_impl.hpp
// =============================================================================

void Router::register_route(HttpMethod method, std::span<const detail::Segment> segs,
                            ErasedHandler handler)
{
    impl_->insert(impl_->insert_root_, segs, method, std::move(handler));
}

// =============================================================================
// Router::dispatch — thread-safe after all register_route calls complete
// =============================================================================

aevox::Task<aevox::Response> Router::dispatch(aevox::Request& req) const
{
    if (!impl_ || !impl_->root_) {
        co_return aevox::Response::not_found("404 Not Found");
    }

    const auto method_idx = static_cast<std::uint8_t>(req.method());
    if (method_idx >= kMethodCount) {
        co_return aevox::Response::bad_request("Unknown HTTP method");
    }

    const std::string_view path = req.path();

    // Split path on '/' into string_views that point into req.path().
    std::vector<std::string_view> segs;
    segs.reserve(8);
    {
        std::size_t pos = (path.size() > 0 && path[0] == '/') ? 1u : 0u;
        while (pos < path.size()) {
            auto end = path.find('/', pos);
            if (end == std::string_view::npos)
                end = path.size();
            if (end > pos)
                segs.push_back(path.substr(pos, end - pos));
            pos = end + 1;
        }
    }

    TrieNode*                                        node = impl_->root_.get();
    std::vector<std::pair<std::string, std::string>> captured;
    captured.reserve(4);

    bool wildcard_matched = false;

    for (std::size_t i = 0; i < segs.size() && !wildcard_matched; ++i) {
        const std::string_view seg = segs[i];

        // 1. Static match
        TrieNode* static_match = nullptr;
        for (auto& child : node->static_children) {
            if (child->segment == seg) {
                static_match = child.get();
                break;
            }
        }
        if (static_match) {
            node = static_match;
            continue;
        }

        // 2. Named-parameter match
        if (node->param_child) {
            captured.emplace_back(node->param_child->segment, std::string{seg});
            node = node->param_child.get();
            continue;
        }

        // 3. Wildcard match — greedy: captures remainder of path via offset arithmetic
        if (node->wildcard_child) {
            const std::size_t offset = static_cast<std::size_t>(seg.data() - path.data());
            captured.emplace_back(node->wildcard_child->segment, std::string{path.substr(offset)});
            node             = node->wildcard_child.get();
            wildcard_matched = true;
            break;
        }

        // No match at this level.
        co_return aevox::Response::not_found("404 Not Found");
    }

    // After processing all path segments, if the current node has a wildcard child
    // that was not yet consumed (e.g. route /files/{path...} dispatched with /files/),
    // capture an empty string and advance.
    if (!wildcard_matched && node->wildcard_child) {
        captured.emplace_back(node->wildcard_child->segment, std::string{});
        node = node->wildcard_child.get();
    }

    // Check for a registered handler at the matched node.
    if (node->handlers[method_idx]) {
        // Inject captured path parameters directly via friend-class access.
        req.impl_->params.clear();
        for (auto& [k, v] : captured)
            req.impl_->params.emplace(k, v);

        co_return co_await node->handlers[method_idx](req);
    }

    if (node->method_mask != 0) {
        // Path matched but the requested method has no handler → 405.
        co_return aevox::Response::method_not_allowed().header("Allow", build_allow_header(
                                                                            node->method_mask));
    }

    co_return aevox::Response::not_found("404 Not Found");
}

// =============================================================================
// Router::group — returns a child Router scoped to prefix
// =============================================================================

Router Router::group(std::string_view prefix)
{
    auto  segs = detail::parse_pattern(prefix);
    auto* node = impl_->insert_root_;
    for (const auto& seg : segs)
        node = impl_->ensure_child(node, seg);

    auto group_impl          = std::make_unique<Impl>();
    group_impl->root_        = nullptr; // group sub-Router does not own the root
    group_impl->insert_root_ = node;
    group_impl->owns_root_   = false;

    return Router{GroupTag{}, std::move(group_impl)};
}

} // namespace aevox

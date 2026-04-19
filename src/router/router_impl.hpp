#pragma once
// src/router/router_impl.hpp
//
// INTERNAL — never included outside src/router/ or test TUs that need direct
// trie access.
//
// Provides complete definitions for:
//   TrieNode     — one node in the radix trie (segment + per-method handler array)
//   Router::Impl — owns the root TrieNode; insert_root_ is the registration point
//   App::Impl    — AppConfig + Router + Executor
//
// Include order note: this header includes <aevox/router.hpp> and <aevox/app.hpp>
// to make Router::Impl and App::Impl visible as nested structs. Those public headers
// transitively pull in include/aevox/router_impl.hpp → src/router/handler_wrap.hpp,
// so all detail:: types (ErasedHandler, ParamType, Segment) are available here.
//
// Design: AEV-004-arch.md §5

#include "router/handler_wrap.hpp"

#include <aevox/app.hpp>
#include <aevox/executor.hpp>
#include <aevox/router.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace aevox {

// =============================================================================
// kMethodCount — number of routable HTTP methods (GET through OPTIONS, 0–6)
// HttpMethod::Unknown == 7 is out of range and must be guarded before indexing.
// =============================================================================
inline constexpr std::size_t kMethodCount = 7;

// =============================================================================
// TrieNode
// =============================================================================

/// One node in the radix trie.
///
/// Each node represents one path segment. static_children are literal matches;
/// param_child matches any single segment; wildcard_child greedily captures
/// the remaining path. At most one param_child and one wildcard_child per level.
///
/// handlers[i] is the registered ErasedHandler for HttpMethod i (cast to uint8_t).
/// method_mask bit i is set iff handlers[i] is non-null.
struct TrieNode
{
    enum class NodeKind : std::uint8_t
    {
        Static,   ///< Literal segment match.
        Param,    ///< Named single-segment capture.
        Wildcard, ///< Greedy tail capture (must be last segment in pattern).
    };

    NodeKind kind{NodeKind::Static};

    /// For Static: the literal segment text.
    /// For Param/Wildcard: the parameter name (as declared in the pattern).
    std::string segment;

    detail::ParamType param_type{detail::ParamType::None};

    /// Per-method handlers. Index = static_cast<uint8_t>(HttpMethod).
    /// Valid indices: 0 (GET) through 6 (OPTIONS). Index 7 (Unknown) is never set.
    std::array<detail::ErasedHandler, kMethodCount> handlers;

    /// Bitmask: bit i set ↔ handlers[i] is registered. Used to build Allow header.
    std::uint8_t method_mask{0};

    std::vector<std::unique_ptr<TrieNode>> static_children;
    std::unique_ptr<TrieNode>              param_child;
    std::unique_ptr<TrieNode>              wildcard_child;
};

// =============================================================================
// Router::Impl
// =============================================================================

struct Router::Impl
{
    /// Owns the root TrieNode for the main Router.
    /// Null for group sub-Routers (they reference a node in the parent's trie).
    std::unique_ptr<TrieNode> root_;

    /// Registration entry point.
    /// = root_.get() for the main Router.
    /// = pointer into a parent's trie for group sub-Routers.
    TrieNode* insert_root_{nullptr};

    /// true iff this Impl owns root_ (i.e. not a group sub-Router).
    bool owns_root_{true};

    // -------------------------------------------------------------------------
    // Registration helpers
    // -------------------------------------------------------------------------

    /// Recursively inserts a route at segs.front(), advancing recursively.
    /// Terminal call (segs empty) registers handler at node.
    void insert(TrieNode*                        node,
                std::span<const detail::Segment> segs,
                HttpMethod                        method,
                detail::ErasedHandler             handler);

    /// Finds or creates a child of node matching seg's kind and name/literal.
    /// Returns the child node pointer (never null — creates if absent).
    TrieNode* ensure_child(TrieNode* node, const detail::Segment& seg);
};

// =============================================================================
// App::Impl
// =============================================================================

struct App::Impl
{
    AppConfig                  config_;
    Router                     router_;
    std::unique_ptr<Executor>  executor_;
};

} // namespace aevox

#pragma once
// src/log/json_escape.hpp
//
// INTERNAL — JSON string escaping per RFC 8259.
//
// Shared by spdlog_backend.cpp and middleware_logger.cpp.
//
// Design: Tasks/architecture/AEV-011-arch.md §11 MIN-3

#include <cstddef>
#include <format>
#include <string>
#include <string_view>

namespace aevox {

inline std::string json_escape(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
                }
                else {
                    out += c;
                }
        }
    }
    return out;
}

} // namespace aevox

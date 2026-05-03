// src/json/json_error.cpp
// Out-of-line definition for aevox::JsonError::message().
//
// Kept out of the header so that the clang static analyser cannot trace
// into the implementation when analysing translation units that move a
// JsonError and then call message() — a valid operation per the class
// contract (moved-from JsonError returns an empty view) that the analyser's
// cplusplus.Move check would otherwise flag as a false positive.

#include <aevox/json_error.hpp>

namespace aevox {

std::string_view JsonError::message() const noexcept
{
    return message_;
}

} // namespace aevox

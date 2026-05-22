#include <aevox/error.hpp>
#include <aevox/middleware.hpp>
#include <aevox/middleware/static_files.hpp>
#include <aevox/request.hpp>
#include <aevox/response.hpp>
#include <aevox/task.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aevox::middleware {
namespace {

constexpr unsigned char kAsciiControlLimit{0x20U};
constexpr unsigned char kAsciiDelete{0x7FU};
constexpr unsigned char kHexAlphaOffset{10U};

struct StaticFilesState
{
    std::filesystem::path                        canonical_root;
    std::string                                  url_prefix;
    std::string                                  index_file;
    std::unordered_map<std::string, std::string> mime_overrides;
};

enum class PathResolutionError : std::uint8_t
{
    Forbidden,
    NotFound,
};

struct ResolvedPath
{
    std::filesystem::path path;
};

[[nodiscard]] bool is_ascii_control(char ch) noexcept
{
    const auto value = static_cast<unsigned char>(ch);
    return value < kAsciiControlLimit || value == kAsciiDelete;
}

[[nodiscard]] bool is_hex_digit(char ch) noexcept
{
    const auto value = static_cast<unsigned char>(ch);
    return std::isxdigit(value) != 0;
}

[[nodiscard]] unsigned char hex_value(char ch) noexcept
{
    if (ch >= '0' && ch <= '9') {
        return static_cast<unsigned char>(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return static_cast<unsigned char>(ch - 'a' + kHexAlphaOffset);
    }
    return static_cast<unsigned char>(ch - 'A' + kHexAlphaOffset);
}

[[nodiscard]] bool has_invalid_url_path_char(std::string_view value) noexcept
{
    return std::ranges::any_of(value, [](char ch) noexcept {
        return ch == '?' || ch == '#' || ch == '\\' || is_ascii_control(ch);
    });
}

[[nodiscard]] bool has_path_dot_segment(std::string_view path) noexcept
{
    std::size_t segment_start = 0;
    while (segment_start <= path.size()) {
        const auto segment_end = path.find('/', segment_start);
        const auto count       = segment_end == std::string_view::npos ? std::string_view::npos
                                                                       : segment_end - segment_start;
        const auto segment     = path.substr(segment_start, count);
        if (segment == "." || segment == "..") {
            return true;
        }
        if (segment_end == std::string_view::npos) {
            return false;
        }
        segment_start = segment_end + 1;
    }
    return false;
}

[[nodiscard]] std::string normalize_url_prefix(std::string prefix)
{
    while (prefix.size() > 1 && prefix.back() == '/') {
        prefix.pop_back();
    }
    return prefix;
}

[[nodiscard]] bool is_valid_url_prefix(std::string_view prefix) noexcept
{
    if (prefix.empty() || prefix.front() != '/') {
        return false;
    }
    if (has_invalid_url_path_char(prefix)) {
        return false;
    }
    return !has_path_dot_segment(prefix);
}

[[nodiscard]] bool is_valid_index_file(std::string_view index_file) noexcept
{
    if (index_file.empty()) {
        return true;
    }
    if (index_file == "." || index_file == "..") {
        return false;
    }
    return !std::ranges::any_of(index_file, [](char ch) noexcept {
        return ch == '/' || ch == '\\' || ch == ':' || is_ascii_control(ch);
    });
}

[[nodiscard]] bool is_valid_mime_key(std::string_view key) noexcept
{
    if (key.size() < 2 || key.front() != '.') {
        return false;
    }
    return std::ranges::all_of(key, [](char ch) noexcept {
        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '+' ||
               ch == '-';
    });
}

[[nodiscard]] bool is_valid_mime_value(std::string_view value) noexcept
{
    if (value.empty()) {
        return false;
    }
    return !std::ranges::any_of(value, [](char ch) noexcept { return is_ascii_control(ch); });
}

[[nodiscard]] bool prefix_matches(std::string_view path, std::string_view prefix) noexcept
{
    if (prefix == "/") {
        return path.starts_with('/');
    }
    return path.starts_with(prefix) && (path.size() == prefix.size() || path[prefix.size()] == '/');
}

[[nodiscard]] std::string_view strip_prefix(std::string_view path, std::string_view prefix) noexcept
{
    if (prefix == "/") {
        return path;
    }
    auto remainder = path.substr(prefix.size());
    if (remainder.empty()) {
        return "/";
    }
    return remainder;
}

[[nodiscard]] std::optional<std::string> percent_decode_path(std::string_view input)
{
    std::string output;
    output.reserve(input.size());

    for (std::size_t pos = 0; pos < input.size(); ++pos) {
        const char ch = input[pos];
        if (ch != '%') {
            if (ch == '\\' || ch == ':' || is_ascii_control(ch)) {
                return std::nullopt;
            }
            output.push_back(ch);
            continue;
        }

        if (pos + 2 >= input.size() || !is_hex_digit(input[pos + 1]) ||
            !is_hex_digit(input[pos + 2]))
        {
            return std::nullopt;
        }

        const auto high    = hex_value(input[pos + 1]);
        const auto low     = hex_value(input[pos + 2]);
        const auto decoded = static_cast<char>(static_cast<unsigned char>((high << 4U) | low));
        if (decoded == '\\' || decoded == ':' || is_ascii_control(decoded)) {
            return std::nullopt;
        }
        output.push_back(decoded);
        pos += 2;
    }

    return output;
}

[[nodiscard]] std::vector<std::string_view> split_validated_segments(std::string_view path)
{
    std::vector<std::string_view> segments;
    std::size_t                   segment_start = 0;
    while (segment_start <= path.size()) {
        const auto segment_end = path.find('/', segment_start);
        const auto count       = segment_end == std::string_view::npos ? std::string_view::npos
                                                                       : segment_end - segment_start;
        const auto segment     = path.substr(segment_start, count);
        if (!segment.empty()) {
            segments.push_back(segment);
        }
        if (segment_end == std::string_view::npos) {
            break;
        }
        segment_start = segment_end + 1;
    }
    return segments;
}

[[nodiscard]] bool is_within_root(const std::filesystem::path& root,
                                  const std::filesystem::path& candidate)
{
    const auto root_end = root.end();
    const auto cand_end = candidate.end();
    auto       root_it  = root.begin();
    auto       cand_it  = candidate.begin();

    while (root_it != root_end && cand_it != cand_end) {
        if (*root_it != *cand_it) {
            return false;
        }
        ++root_it;
        ++cand_it;
    }

    return root_it == root_end;
}

[[nodiscard]] std::expected<std::filesystem::path, PathResolutionError> resolve_candidate(
    const StaticFilesState& state, const std::filesystem::path& candidate)
{
    std::error_code ec;
    auto            resolved = std::filesystem::weakly_canonical(candidate, ec);
    if (ec) {
        return std::unexpected(PathResolutionError::NotFound);
    }
    if (!is_within_root(state.canonical_root, resolved)) {
        return std::unexpected(PathResolutionError::Forbidden);
    }
    return resolved;
}

[[nodiscard]] bool is_permission_denied(const std::error_code& ec) noexcept
{
    return ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted;
}

[[nodiscard]] std::expected<ResolvedPath, PathResolutionError> resolve_request_path(
    const StaticFilesState& state, std::string_view request_path)
{
    auto decoded = percent_decode_path(request_path);
    if (!decoded.has_value()) {
        return std::unexpected(PathResolutionError::Forbidden);
    }

    auto candidate = state.canonical_root;
    for (const auto segment : split_validated_segments(*decoded)) {
        if (segment == "." || segment == "..") {
            return std::unexpected(PathResolutionError::Forbidden);
        }
        candidate /= std::filesystem::path{std::string{segment}};
    }

    auto resolved = resolve_candidate(state, candidate);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }

    std::error_code ec;
    if (std::filesystem::is_directory(*resolved, ec)) {
        if (is_permission_denied(ec)) {
            return std::unexpected(PathResolutionError::Forbidden);
        }
        if (ec) {
            return std::unexpected(PathResolutionError::NotFound);
        }
        if (state.index_file.empty()) {
            return std::unexpected(PathResolutionError::NotFound);
        }

        auto indexed = *resolved / state.index_file;
        resolved     = resolve_candidate(state, indexed);
        if (!resolved) {
            return std::unexpected(resolved.error());
        }
    }

    if (!std::filesystem::is_regular_file(*resolved, ec)) {
        if (is_permission_denied(ec)) {
            return std::unexpected(PathResolutionError::Forbidden);
        }
        return std::unexpected(PathResolutionError::NotFound);
    }

    return ResolvedPath{.path = std::move(*resolved)};
}

[[nodiscard]] std::string lowercase_ascii(std::string_view input)
{
    std::string output;
    output.reserve(input.size());
    std::ranges::transform(input, std::back_inserter(output), [](char ch) noexcept {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    });
    return output;
}

[[nodiscard]] std::string_view default_mime_type(std::string_view extension) noexcept
{
    constexpr std::array kDefaults{
        std::pair{std::string_view{".html"}, std::string_view{"text/html"}},
        std::pair{std::string_view{".htm"}, std::string_view{"text/html"}},
        std::pair{std::string_view{".css"}, std::string_view{"text/css"}},
        std::pair{std::string_view{".js"}, std::string_view{"application/javascript"}},
        std::pair{std::string_view{".mjs"}, std::string_view{"application/javascript"}},
        std::pair{std::string_view{".json"}, std::string_view{"application/json"}},
        std::pair{std::string_view{".png"}, std::string_view{"image/png"}},
        std::pair{std::string_view{".jpg"}, std::string_view{"image/jpeg"}},
        std::pair{std::string_view{".jpeg"}, std::string_view{"image/jpeg"}},
        std::pair{std::string_view{".gif"}, std::string_view{"image/gif"}},
        std::pair{std::string_view{".svg"}, std::string_view{"image/svg+xml"}},
        std::pair{std::string_view{".ico"}, std::string_view{"image/x-icon"}},
        std::pair{std::string_view{".woff2"}, std::string_view{"font/woff2"}},
        std::pair{std::string_view{".txt"}, std::string_view{"text/plain"}},
        std::pair{std::string_view{".pdf"}, std::string_view{"application/pdf"}},
    };

    const auto it = std::ranges::find_if(kDefaults, [extension](const auto& entry) noexcept {
        return entry.first == extension;
    });
    if (it == kDefaults.end()) {
        return "application/octet-stream";
    }
    return it->second;
}

[[nodiscard]] std::string mime_type_for(const StaticFilesState&      state,
                                        const std::filesystem::path& path)
{
    const auto extension = lowercase_ascii(path.extension().string());
    const auto override  = state.mime_overrides.find(extension);
    if (override != state.mime_overrides.end()) {
        return override->second;
    }
    return std::string{default_mime_type(extension)};
}

[[nodiscard]] std::expected<std::uintmax_t, PathResolutionError> read_file_size(
    const std::filesystem::path& path)
{
    std::error_code ec;
    const auto      size = std::filesystem::file_size(path, ec);
    if (!ec) {
        return size;
    }
    if (is_permission_denied(ec)) {
        return std::unexpected(PathResolutionError::Forbidden);
    }
    return std::unexpected(PathResolutionError::NotFound);
}

[[nodiscard]] std::optional<std::string> read_file_body(const std::filesystem::path& path,
                                                        std::uintmax_t               size)
{
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        return std::nullopt;
    }

    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return std::nullopt;
    }

    std::string body(static_cast<std::size_t>(size), '\0');
    if (!body.empty()) {
        file.read(body.data(), static_cast<std::streamsize>(body.size()));
    }
    if (!file && file.gcount() != static_cast<std::streamsize>(body.size())) {
        return std::nullopt;
    }
    return body;
}

[[nodiscard]] Response response_for_resolution_error(PathResolutionError error)
{
    if (error == PathResolutionError::Forbidden) {
        return Response::forbidden();
    }
    return Response::not_found();
}

[[nodiscard]] std::expected<std::shared_ptr<const StaticFilesState>, StaticFilesConfigError>
make_state(StaticFilesConfig config)
{
    if (config.root.empty()) {
        return std::unexpected(StaticFilesConfigError::RootEmpty);
    }
    if (!config.root.is_absolute()) {
        return std::unexpected(StaticFilesConfigError::RootRelative);
    }

    std::error_code ec;
    auto            canonical_root = std::filesystem::canonical(config.root, ec);
    if (ec) {
        return std::unexpected(StaticFilesConfigError::RootNotFound);
    }
    if (!std::filesystem::is_directory(canonical_root, ec) || ec) {
        return std::unexpected(StaticFilesConfigError::RootNotDirectory);
    }

    if (!is_valid_url_prefix(config.url_prefix)) {
        return std::unexpected(StaticFilesConfigError::InvalidUrlPrefix);
    }
    config.url_prefix = normalize_url_prefix(std::move(config.url_prefix));

    if (!is_valid_index_file(config.index_file)) {
        return std::unexpected(StaticFilesConfigError::InvalidIndexFile);
    }

    for (const auto& [key, value] : config.mime_overrides) {
        if (!is_valid_mime_key(key) || !is_valid_mime_value(value)) {
            return std::unexpected(StaticFilesConfigError::InvalidMimeOverride);
        }
    }

    auto state = std::make_shared<StaticFilesState>(
        StaticFilesState{.canonical_root = std::move(canonical_root),
                         .url_prefix     = std::move(config.url_prefix),
                         .index_file     = std::move(config.index_file),
                         .mime_overrides = std::move(config.mime_overrides)});
    return std::shared_ptr<const StaticFilesState>{std::move(state)};
}

} // namespace

std::string_view to_string(StaticFilesConfigError error) noexcept
{
    switch (error) {
        case StaticFilesConfigError::RootEmpty:
            return "root is empty";
        case StaticFilesConfigError::RootRelative:
            return "root is relative";
        case StaticFilesConfigError::RootNotFound:
            return "root not found";
        case StaticFilesConfigError::RootNotDirectory:
            return "root is not a directory";
        case StaticFilesConfigError::InvalidUrlPrefix:
            return "invalid URL prefix";
        case StaticFilesConfigError::InvalidIndexFile:
            return "invalid index file";
        case StaticFilesConfigError::InvalidMimeOverride:
            return "invalid MIME override";
    }
    return "unknown static files config error";
}

aevox::ErrorCategory category(StaticFilesConfigError error) noexcept
{
    switch (error) {
        case StaticFilesConfigError::RootNotFound:
            return aevox::ErrorCategory::NotFound;
        case StaticFilesConfigError::RootEmpty:
        case StaticFilesConfigError::RootRelative:
        case StaticFilesConfigError::RootNotDirectory:
        case StaticFilesConfigError::InvalidUrlPrefix:
        case StaticFilesConfigError::InvalidIndexFile:
        case StaticFilesConfigError::InvalidMimeOverride:
            return aevox::ErrorCategory::Validation;
    }
    return aevox::ErrorCategory::Unknown;
}

std::expected<aevox::Middleware, StaticFilesConfigError> static_files(StaticFilesConfig config)
{
    auto state = make_state(std::move(config));
    if (!state) {
        return std::unexpected(state.error());
    }

    return aevox::Middleware{
        [state = std::move(
             *state)](aevox::Request&                                                        req,
                      std::move_only_function<aevox::Task<aevox::Response>(aevox::Request&)> next)
            -> aevox::Task<aevox::Response> {
            if (!prefix_matches(req.path(), state->url_prefix)) {
                co_return co_await next(req);
            }

            const auto method = req.method();
            if (method != aevox::HttpMethod::GET && method != aevox::HttpMethod::HEAD) {
                co_return aevox::Response::method_not_allowed().header("Allow", "GET, HEAD");
            }

            const auto request_path = strip_prefix(req.path(), state->url_prefix);
            auto       resolved     = resolve_request_path(*state, request_path);
            if (!resolved) {
                co_return response_for_resolution_error(resolved.error());
            }

            const auto file_size = read_file_size(resolved->path);
            if (!file_size) {
                co_return response_for_resolution_error(file_size.error());
            }

            const auto content_length = std::to_string(*file_size);
            const auto mime_type      = mime_type_for(*state, resolved->path);
            if (method == aevox::HttpMethod::HEAD) {
                co_return aevox::Response::ok().content_type(mime_type).header("Content-Length",
                                                                               content_length);
            }

            auto body = read_file_body(resolved->path, *file_size);
            if (!body.has_value()) {
                co_return aevox::Response::not_found();
            }

            co_return aevox::Response::ok(*body).content_type(mime_type).header("Content-Length",
                                                                                content_length);
        }};
}

} // namespace aevox::middleware

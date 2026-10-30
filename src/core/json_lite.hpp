// Minimal, allocation-light, read-only JSON navigation.
//
// The widget only needs to pull a handful of fields out of small API
// responses, and to patch three values inside the Claude Code credentials
// file without disturbing anything else. A full DOM library would be
// overkill, so this works directly on byte spans of the original text.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace cub::json {

/// Byte range [begin, end) of one JSON value inside a document.
struct Span {
    std::size_t begin = 0;
    std::size_t end = 0;

    [[nodiscard]] std::size_t size() const noexcept { return end - begin; }
};

inline constexpr std::size_t npos = std::string_view::npos;

/// Index of the first non-whitespace byte at or after `pos`.
[[nodiscard]] std::size_t skip_ws(std::string_view doc, std::size_t pos) noexcept;

/// One past the end of the value starting at `pos` (after whitespace), or npos if malformed.
[[nodiscard]] std::size_t value_end(std::string_view doc, std::size_t pos) noexcept;

/// Span of the top-level value of `doc`.
[[nodiscard]] std::optional<Span> root(std::string_view doc) noexcept;

/// Direct member `key` of the object at `obj`. Nested objects are not searched.
[[nodiscard]] std::optional<Span> member(std::string_view doc, Span obj,
                                         std::string_view key) noexcept;

[[nodiscard]] bool is_object(std::string_view doc, Span v) noexcept;
[[nodiscard]] bool is_array(std::string_view doc, Span v) noexcept;
[[nodiscard]] bool is_null(std::string_view doc, Span v) noexcept;

/// Decoded string value (escapes and \uXXXX resolved to UTF-8).
[[nodiscard]] std::optional<std::string> as_string(std::string_view doc, Span v);

/// Numeric value; locale independent.
[[nodiscard]] std::optional<double> as_number(std::string_view doc, Span v) noexcept;

/// Invokes `fn(Span)` for each element of the array at `arr`. Stops early if fn returns false.
template <class Fn>
void for_each_element(std::string_view doc, Span arr, Fn&& fn) {
    if (!is_array(doc, arr)) {
        return;
    }
    std::size_t i = skip_ws(doc, arr.begin + 1);
    if (i >= arr.end || doc[i] == ']') {
        return;
    }
    while (i < arr.end) {
        const std::size_t e = value_end(doc, i);
        if (e == npos || e > arr.end) {
            return;
        }
        if (!fn(Span{i, e})) {
            return;
        }
        i = skip_ws(doc, e);
        if (i < arr.end && doc[i] == ',') {
            i = skip_ws(doc, i + 1);
            continue;
        }
        return;
    }
}

/// Encodes `s` as a quoted JSON string literal.
[[nodiscard]] std::string quote(std::string_view s);

}  // namespace cub::json

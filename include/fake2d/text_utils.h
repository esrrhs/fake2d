#pragma once

// Small text utilities every game ends up re-rolling: UTF-8 codepoint walks
// (fonts, cut-to-width, first-glyph badges) and printf-style formatting with
// growth instead of truncation. Compiled from the 1627 project's canonical
// implementations so hosts can drop their local copies.

#include <string>
#include <string_view>
#include <vector>

namespace fake2d {

/// UTF-8 helpers. Decode is forgiving like the renderer's own walks: a
/// truncated sequence yields the lead byte's value and consumes what is
/// there, so one bad byte cannot truncate the rest of the string.
namespace utf8 {

/// Append every codepoint of `s` to `out` (cleared first).
void DecodeInto(std::string_view s, std::vector<unsigned> &out);
/// Every codepoint of `s`.
[[nodiscard]] std::vector<unsigned> Decode(std::string_view s);
/// Codepoint count (invalid bytes count as one each, matching Decode).
[[nodiscard]] std::size_t Count(std::string_view s);
/// First `max_codepoints` codepoints as a UTF-8 string.
[[nodiscard]] std::string Cut(std::string_view s, std::size_t max_codepoints);
/// First codepoint re-encoded ("" when empty).
[[nodiscard]] std::string First(std::string_view s);
/// Encode one codepoint (4-byte max, no validation of surrogates).
[[nodiscard]] std::string Encode(unsigned cp);

} // namespace utf8

/// printf-style formatting that grows instead of truncating: the result is
/// always the fully formatted string (std::string vsnprintf semantics with a
/// stack fast path).
[[nodiscard]] std::string Strf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/// va_list variant for forwarding wrappers.
[[nodiscard]] std::string StrfV(const char *fmt, va_list ap) __attribute__((format(printf, 1, 0)));

} // namespace fake2d

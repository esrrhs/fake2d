#include "fake2d/text_utils.h"

#include <cstdarg>
#include <cstdio>

namespace fake2d {

namespace utf8 {

void DecodeInto(std::string_view s, std::vector<unsigned> &out) {
    out.clear();
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp;
        int n;
        if (c < 0x80) {
            cp = c;
            n = 1;
        } else if ((c >> 5) == 6) {
            cp = c & 0x1f;
            n = 2;
        } else if ((c >> 4) == 14) {
            cp = c & 0x0f;
            n = 3;
        } else {
            cp = c & 0x07;
            n = 4;
        }
        for (int k = 1; k < n && i + k < s.size(); ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3f);
        }
        out.push_back(cp);
        i += static_cast<std::size_t>(n);
    }
}

std::vector<unsigned> Decode(std::string_view s) {
    std::vector<unsigned> out;
    DecodeInto(s, out);
    return out;
}

std::size_t Count(std::string_view s) {
    return Decode(s).size();
}

std::string Cut(std::string_view s, std::size_t max_codepoints) {
    std::string out;
    out.reserve(s.size());
    std::size_t k = 0;
    for (std::size_t i = 0; i < s.size() && k < max_codepoints;) {
        const unsigned char ch = static_cast<unsigned char>(s[i]);
        const int len = ch < 0x80 ? 1 : (ch >> 5) == 6 ? 2 : (ch >> 4) == 14 ? 3 : 4;
        out.append(s.substr(i, static_cast<std::size_t>(len)));
        i += static_cast<std::size_t>(len);
        ++k;
    }
    return out;
}

std::string First(std::string_view s) {
    const std::vector<unsigned> cps = Decode(s);
    if (cps.empty()) {
        return {};
    }
    return Encode(cps.front());
}

std::string Encode(unsigned cp) {
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xc0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xe0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        s += static_cast<char>(0x80 | (cp & 0x3f));
    } else {
        s += static_cast<char>(0xf0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        s += static_cast<char>(0x80 | (cp & 0x3f));
    }
    return s;
}

} // namespace utf8

std::string StrfV(const char *fmt, va_list ap) {
    char buf[1024];
    va_list ap2;
    va_copy(ap2, ap);
    const int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
    std::string out;
    if (n >= static_cast<int>(sizeof buf)) {
        out.resize(static_cast<std::size_t>(n) + 1);
        std::vsnprintf(out.data(), static_cast<std::size_t>(n) + 1, fmt, ap2);
        out.resize(static_cast<std::size_t>(n));
    } else {
        out = n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string();
    }
    va_end(ap2);
    va_end(ap);
    return out;
}

std::string Strf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string out = StrfV(fmt, ap);
    va_end(ap);
    return out;
}

} // namespace fake2d

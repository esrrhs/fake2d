#include "fake2d/i18n.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fake2d {

namespace {

std::string Trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string Unescape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out.push_back(text[i]);
            continue;
        }
        switch (text[++i]) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case '\\': out.push_back('\\'); break;
            default:
                out.push_back('\\');
                out.push_back(text[i]);
                break;
        }
    }
    return out;
}

std::string ToLower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// ---- segmented fallback (CJK source -> non-CJK pack) ---------------------
// Ported from the 1627 project, where dictionary longest-match segmentation
// carries the whole UI when packs only cover part of the source strings.

unsigned DecodeOne(std::string_view s, std::size_t i, std::size_t &len) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { len = 1; return c; }
    if ((c >> 5) == 6 && i + 1 < s.size()) {
        len = 2;
        return ((c & 0x1f) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3f);
    }
    if ((c >> 4) == 14 && i + 2 < s.size()) {
        len = 3;
        return ((c & 0x0f) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3f) << 6) |
               (static_cast<unsigned char>(s[i + 2]) & 0x3f);
    }
    if ((c >> 3) == 30 && i + 3 < s.size()) {
        len = 4;
        return ((c & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3f) << 12) |
               ((static_cast<unsigned char>(s[i + 2]) & 0x3f) << 6) |
               (static_cast<unsigned char>(s[i + 3]) & 0x3f);
    }
    len = 1;
    return c;
}

bool IsCjkCodepoint(unsigned cp) {
    return (cp >= 0x3400 && cp <= 0x9fff) || (cp >= 0x3000 && cp <= 0x303f) ||
           (cp >= 0xff00 && cp <= 0xffef);
}

bool HasCjk(std::string_view s) {
    for (std::size_t i = 0, l = 0; i < s.size(); i += l) {
        if (IsCjkCodepoint(DecodeOne(s, i, l))) return true;
    }
    return false;
}

void AppendPiece(std::string &out, std::string_view piece) {
    if (piece.empty()) return;
    const char a = out.empty() ? ' ' : out.back();
    const char b = piece.front();
    if ((std::isalnum(static_cast<unsigned char>(a)) || a == ')') &&
        (std::isalnum(static_cast<unsigned char>(b)) || b == '(')) {
        out += ' ';
    }
    out.append(piece);
}

const char *PunctWest(unsigned cp) {
    switch (cp) {
        case 0xff0c: case 0x3001: return ", ";   // ， 、
        case 0x3002: return ". ";                 // 。
        case 0xff1a: return ": ";                 // ：
        case 0xff1b: return "; ";                 // ；
        case 0xff08: return " (";                 // （
        case 0xff09: return ")";                  // ）
        case 0x300c: case 0x300d: case 0x300e: case 0x300f: return "\"";  // 「」『』
        case 0x3010: return "[";                  // 【
        case 0x3011: return "] ";                // 】
        case 0x3000: return "  ";                 // 　
        case 0xff01: return "! ";                 // ！
        case 0xff1f: return "? ";                 // ？
        default: return nullptr;
    }
}

// ---- printf format safety -----------------------------------------------
// Ported from the 1627 project's platform/i18n.cpp (fmt_sig): a translated
// format string with a different conversion sequence is dropped at load time
// instead of crashing the caller's printf at runtime.

// Everything between '%' and the conversion character. A space is
// deliberately NOT a modifier: "15% done" must not parse "% d" into a
// conversion, otherwise two harmless stray percents could disagree.
bool IsFormatModifier(char c) {
    if (c >= '0' && c <= '9') return true;
    switch (c) {
        case '.': case '-': case '+': case '#': case '*': case '$':
        case 'h': case 'l': case 'L': case 'z': case 'j': case 't':
        case 'q': case 'I':
            return true;
        default:
            return false;
    }
}

bool IsFormatConversion(char c) {
    switch (c) {
        case 'd': case 'i': case 'o': case 'u': case 'x': case 'X':
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
        case 'a': case 'A': case 'c': case 's': case 'p': case 'n':
            return true;
        default:
            return false;
    }
}

} // namespace

std::string I18n::FormatSignature(std::string_view text) {
    std::string signature;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%' || i + 1 >= text.size()) continue;
        if (text[i + 1] == '%') {   // escaped percent, not a conversion
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        while (j < text.size() && IsFormatModifier(text[j])) ++j;
        if (j < text.size() && IsFormatConversion(text[j])) {
            signature.push_back(text[j]);
            i = j;
        }
        // A stray '%' followed by ordinary text contributes nothing.
    }
    return signature;
}

I18n::~I18n() = default;

I18n::LanguagePack *I18n::Find(std::string_view code) {
    for (LanguagePack &language : languages_) {
        if (language.code == code) {
            return &language;
        }
    }
    return nullptr;
}

const I18n::LanguagePack *I18n::Find(std::string_view code) const {
    for (const LanguagePack &language : languages_) {
        if (language.code == code) {
            return &language;
        }
    }
    return nullptr;
}

bool I18n::LoadDirectory(const std::string &dir, std::string_view default_code) {
    languages_.clear();
    current_.clear();
    ClearMissing();
    ClearRejected();
    seg_caches_.clear();

    std::error_code ec;
    std::vector<std::string> files;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file(ec)) {
            continue;
        }
        if (entry.path().extension() == ".tsv") {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    for (const std::string &path : files) {
        const std::string code =
            std::filesystem::path(path).stem().string();
        LoadFile(path, code);
    }
    if (languages_.empty()) {
        return false;
    }
    if (!default_code.empty() && Find(default_code) != nullptr) {
        current_ = std::string(default_code);
    } else {
        current_ = languages_.front().code;
    }
    return true;
}

int I18n::LoadFile(const std::string &path, std::string_view code) {
    std::ifstream in(path);
    if (!in || code.empty()) {
        return 0;
    }

    LanguagePack *language = Find(code);
    if (language == nullptr) {
        languages_.push_back(LanguagePack{std::string(code), std::string(code), {}});
        language = &languages_.back();
    }
    // Entries just changed: drop this pack's segment index and memo.
    seg_caches_.erase(std::string(code));

    int parsed = 0;
    std::string line;
    bool first_line = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (first_line) {
            first_line = false;
            // Editors on Windows often save TSV with a UTF-8 BOM; left in
            // place it would turn the first key/comment into garbage.
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }
        }
        if (line.empty()) {
            continue;
        }
        if (line[0] == '#') {
            const std::string_view comment = std::string_view(line).substr(1);
            const std::size_t colon = comment.find(':');
            if (colon != std::string_view::npos &&
                Trim(comment.substr(0, colon)) == "name") {
                const std::string name = Trim(comment.substr(colon + 1));
                if (!name.empty()) {
                    language->name = name;
                }
            }
            continue;
        }
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        const std::string key = Unescape(std::string_view(line).substr(0, tab));
        if (key.empty()) {
            continue;
        }
        const std::string value = Unescape(std::string_view(line).substr(tab + 1));
        // A translation that changes the ordered printf conversions would be
        // undefined behaviour at the call site. Drop just that entry so the
        // source string renders verbatim instead, and keep a diagnostic.
        if (format_guard_ && FormatSignature(key) != FormatSignature(value)) {
            ++rejected_count_;
            if (rejected_order_.size() < kMaxRejected) {
                rejected_order_.push_back(key);
            }
            continue;
        }
        language->entries[key] = value;
        ++parsed;
    }
    return parsed;
}

bool I18n::SetLanguage(std::string_view code) {
    if (Find(code) == nullptr) {
        return false;
    }
    if (current_ != code) {
        // A key missing in one pack may exist in another; the report is
        // per-language, so switching starts a fresh list.
        ClearMissing();
    }
    current_ = std::string(code);
    return true;
}

bool I18n::IsCjk() const {
    const std::string code = ToLower(current_);
    return code.rfind("zh", 0) == 0 || code.rfind("ja", 0) == 0 ||
           code.rfind("ko", 0) == 0;
}

std::string_view I18n::Translate(std::string_view key) const {
    const LanguagePack *language = Find(current_);
    if (language == nullptr) {
        return key;
    }
    const auto it = language->entries.find(std::string(key));
    if (it == language->entries.end()) {
        if (track_missing_) {
            if (missing_.size() < 512 &&
                missing_.insert(std::string(key)).second) {
                missing_order_.emplace_back(key);
            }
        }
        return key;
    }
    return it->second;
}

std::string I18n::TranslateSegmented(std::string_view key) const {
    const LanguagePack *language = Find(current_);
    if (language == nullptr) {
        return std::string(key);
    }
    const auto exact = language->entries.find(std::string(key));
    if (exact != language->entries.end()) {
        return exact->second;
    }
    // Fallback only makes sense translating CJK source into a non-CJK pack.
    if (IsCjk() || !HasCjk(key)) {
        return std::string(key);
    }

    SegCache &cache = seg_caches_[current_];
    if (const auto hit = cache.memo.find(std::string(key));
        hit != cache.memo.end()) {
        return hit->second;
    }
    if (!cache.built) {
        for (const auto &[k, v] : language->entries) {
            if (k.empty() || k.size() > 120 || k.find('%') != std::string::npos ||
                k.find('\n') != std::string::npos) {
                continue;
            }
            std::size_t l = 0;
            const unsigned cp = DecodeOne(k, 0, l);
            if (!IsCjkCodepoint(cp) || v == k) {
                continue;
            }
            cache.by_first[cp].push_back({&k, &v});
        }
        for (auto &[cp, list] : cache.by_first) {
            std::sort(list.begin(), list.end(),
                      [](const SegCache::Entry &a, const SegCache::Entry &b) {
                          return a.key->size() > b.key->size();
                      });
        }
        cache.built = true;
    }

    std::string out;
    out.reserve(key.size() + 16);
    for (std::size_t i = 0; i < key.size();) {
        std::size_t l = 0;
        const unsigned cp = DecodeOne(key, i, l);
        if (IsCjkCodepoint(cp)) {
            bool hit = false;
            if (const auto it = cache.by_first.find(cp); it != cache.by_first.end()) {
                for (const SegCache::Entry &e : it->second) {
                    if (key.compare(i, e.key->size(), *e.key) == 0) {
                        AppendPiece(out, *e.value);
                        i += e.key->size();
                        hit = true;
                        break;
                    }
                }
            }
            if (hit) continue;
            if (const char *p = PunctWest(cp)) {
                out += p;
            } else {
                out.append(key.substr(i, l));
            }
            i += l;
        } else {
            out.append(key.substr(i, l));
            i += l;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();

    constexpr std::size_t kMemoCap = 60000;
    if (cache.memo.size() < kMemoCap) {
        cache.memo.emplace(std::string(key), out);
    }
    return out;
}

std::string_view I18n::MissingKey(std::size_t index) const {
    return index < missing_order_.size() ? std::string_view(missing_order_[index])
                                         : std::string_view{};
}

void I18n::ClearMissing() {
    missing_.clear();
    missing_order_.clear();
}

std::string_view I18n::RejectedKey(std::size_t index) const {
    return index < rejected_order_.size()
               ? std::string_view(rejected_order_[index])
               : std::string_view{};
}

void I18n::ClearRejected() {
    rejected_count_ = 0;
    rejected_order_.clear();
}

bool I18n::Has(std::string_view key) const {
    const LanguagePack *language = Find(current_);
    return language != nullptr &&
           language->entries.find(std::string(key)) != language->entries.end();
}

const I18n::LanguagePack *I18n::GetLanguage(std::size_t index) const {
    return index < languages_.size() ? &languages_[index] : nullptr;
}

std::size_t I18n::EntryCount(std::string_view code) const {
    const LanguagePack *language = Find(code);
    return language != nullptr ? language->entries.size() : 0;
}

void I18n::Clear() {
    languages_.clear();
    current_.clear();
    ClearMissing();
    ClearRejected();
    seg_caches_.clear();
}

} // namespace fake2d

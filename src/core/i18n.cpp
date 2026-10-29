#include "fake2d/i18n.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

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

} // namespace

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
        language->entries[key] = Unescape(std::string_view(line).substr(tab + 1));
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

std::string_view I18n::MissingKey(std::size_t index) const {
    return index < missing_order_.size() ? std::string_view(missing_order_[index])
                                         : std::string_view{};
}

void I18n::ClearMissing() {
    missing_.clear();
    missing_order_.clear();
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
}

} // namespace fake2d

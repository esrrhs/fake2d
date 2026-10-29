#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fake2d {

/// Runtime localization table (gettext-style, source text is the key).
///
/// Language packs are UTF-8 `.tsv` files named `<code>.tsv`:
///
/// ```
/// # name: 简体中文
/// Hello\t你好
/// Save\t存档
/// ```
///
/// `\n`, `\t` and `\\` are decoded; a `# name:` comment names the language.
/// Switching is instant (the table is looked up at draw time), and mods can
/// append/override entries with LoadFile. Unknown keys pass through, so a
/// partially translated game still renders.
class I18n {
public:
    struct LanguagePack {
        std::string code;
        std::string name;
        std::unordered_map<std::string, std::string> entries;
    };

    /// Replace the table with every `<dir>/*.tsv` (file stem = language code)
    /// and activate `default_code` (empty = the first file). Returns false
    /// when no language file could be read.
    bool LoadDirectory(const std::string &dir, std::string_view default_code = {});

    /// Add or override entries for one language (mods). The language is
    /// created when absent; later files win. Returns parsed entry count.
    int LoadFile(const std::string &path, std::string_view code);

    bool SetLanguage(std::string_view code);
    [[nodiscard]] const std::string &Language() const { return current_; }
    /// True for zh/ja/ko language codes (CJK line breaking / wide glyphs).
    [[nodiscard]] bool IsCjk() const;

    /// Translate a source-language key; unknown keys are returned verbatim.
    [[nodiscard]] std::string_view Translate(std::string_view key) const;
    [[nodiscard]] bool Has(std::string_view key) const;

    [[nodiscard]] std::size_t LanguageCount() const { return languages_.size(); }
    [[nodiscard]] const LanguagePack *GetLanguage(std::size_t index) const;
    [[nodiscard]] std::size_t EntryCount(std::string_view code) const;

    void Clear();

private:
    LanguagePack *Find(std::string_view code);
    [[nodiscard]] const LanguagePack *Find(std::string_view code) const;

    std::vector<LanguagePack> languages_;
    std::string current_;
};

} // namespace fake2d

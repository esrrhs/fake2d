#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

    // --- missing-key diagnostics (development aid) -----------------------
    /// Record keys that the active pack does not define (capped at 512) so a
    /// release build can report untranslated strings instead of silently
    /// falling back. Enabled by default; lookups of known keys cost nothing.
    void SetTrackMissing(bool track) { track_missing_ = track; }
    [[nodiscard]] bool TrackingMissing() const { return track_missing_; }
    [[nodiscard]] std::size_t MissingCount() const { return missing_.size(); }
    /// Key at a stable index ("" when out of range).
    [[nodiscard]] std::string_view MissingKey(std::size_t index) const;
    void ClearMissing();

    void Clear();

private:
    LanguagePack *Find(std::string_view code);
    [[nodiscard]] const LanguagePack *Find(std::string_view code) const;

    std::vector<LanguagePack> languages_;
    std::string current_;
    bool track_missing_ = true;
    // Filled from const Translate() calls (diagnostics, not game state).
    mutable std::unordered_set<std::string> missing_;
    mutable std::vector<std::string> missing_order_;
};

} // namespace fake2d

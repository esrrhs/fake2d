#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace fake2d {

class I18n;

/// Data-pack mod discovery and ordering.
///
/// A mod is a directory with a `mod.json` manifest:
///
/// ```json
/// { "name": "More Goods", "version": "1.2", "enabled": true, "load_order": 10 }
/// ```
///
/// `Scan()` collects every such directory under the given roots, sorts them by
/// `load_order` (stable, so equal orders keep the directory order) and exposes
/// them to scripts. `Resolve()` lets later mods override files from earlier
/// ones and the base game, `LoadLanguages()` merges each active mod's
/// `lang/*.tsv` into the shared I18n table. Games keep ownership of their own
/// data formats — the manifest/order/i18n plumbing is what is generic.
class ModManager {
public:
    struct ModInfo {
        std::string name;
        std::string version;
        std::string path; // absolute or root-relative directory
        bool enabled = true;
        int load_order = 0;
    };

    /// Replace the list with the mods found under `dirs`.
    void Scan(const std::vector<std::string> &dirs);
    /// Append mods found under `dirs` (keeps the existing list).
    void ScanMore(const std::vector<std::string> &dirs);
    void Clear();

    [[nodiscard]] const std::vector<ModInfo> &Mods() const { return mods_; }
    [[nodiscard]] std::size_t Count() const { return mods_.size(); }
    [[nodiscard]] std::size_t ActiveCount() const;
    /// Name of the i-th active mod in load order ("" when out of range).
    [[nodiscard]] std::string ActiveName(std::size_t index) const;
    [[nodiscard]] bool Enabled(std::string_view name) const;
    /// Toggle a mod by name; false when no such mod exists.
    bool SetEnabled(std::string_view name, bool enabled);

    /// Base game directory used as the final fallback by Resolve().
    void SetBaseDir(std::string dir) { base_dir_ = std::move(dir); }
    [[nodiscard]] const std::string &BaseDir() const { return base_dir_; }

    /// First existing `relative` in active mods (last load order wins), then
    /// the base directory. Returns "" when nothing matches.
    [[nodiscard]] std::string Resolve(std::string_view relative) const;

    /// Merge `<mod>/lang/*.tsv` of every active mod into `i18n`.
    /// Returns the number of parsed entries.
    int LoadLanguages(I18n &i18n) const;

    /// Human-readable one-line-per-mod report (for logs and mod screens).
    [[nodiscard]] std::string Report() const;

private:
    void SortMods();

    std::vector<ModInfo> mods_;
    std::string base_dir_;
};

} // namespace fake2d

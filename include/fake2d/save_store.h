#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace fake2d {

/// Persistent key/value store for game saves (number or string values).
///
/// The in-memory map is the live database; LoadSlot() replaces it with the
/// contents of `<dir>/<slot>.json`, SaveSlot() writes it back atomically.
/// Slot names are restricted to [A-Za-z0-9_-]{1,32}: saves can never escape
/// the configured save directory.
class SaveStore {
public:
    static constexpr std::size_t kMaxSlotNameLen = 32;

    static bool ValidSlotName(std::string_view slot);

    /// Replace in-memory contents from <dir>/<slot>.json. A missing file is a
    /// valid empty store (returns true with an empty map); bad JSON/paths
    /// return false and leave memory untouched.
    bool LoadSlot(std::string_view slot, std::string_view dir);
    /// Write in-memory contents to <dir>/<slot>.json (tmp file + rename).
    [[nodiscard]] bool SaveSlot(std::string_view slot,
                                std::string_view dir) const;

    void Reset() { values_.clear(); }

    void SetNumber(std::string_view key, double value);
    void SetString(std::string_view key, std::string_view value);
    [[nodiscard]] double GetNumber(std::string_view key, double fallback) const;
    [[nodiscard]] std::string GetString(std::string_view key,
                                        std::string_view fallback) const;
    [[nodiscard]] bool Has(std::string_view key) const;
    void Erase(std::string_view key);
    [[nodiscard]] std::size_t Size() const { return values_.size(); }

private:
    struct Value {
        bool is_string = false;
        double number = 0.0;
        std::string text;
    };
    std::unordered_map<std::string, Value> values_;
};

} // namespace fake2d

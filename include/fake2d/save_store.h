#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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

    // --- binary save containers (v2) -------------------------------------
    /// Metadata for a blob slot (`<dir>/<slot>.sav`).
    struct BlobInfo {
        std::uint32_t game_version = 0;   // caller-defined save schema
        std::uint32_t payload_bytes = 0;
        std::uint32_t payload_crc = 0;
        std::string header;               // optional UTF-8 key=value lines
    };

    /// Write an opaque payload as `F2SV | container version | game version |
    /// header length | header | CRC32 | payload` (tmp file + rename). The
    /// header carries game metadata (slot name, timestamp, map id, ...).
    [[nodiscard]] bool SaveBlob(std::string_view slot, std::string_view dir,
                                std::string_view payload,
                                std::string_view header = {},
                                std::uint32_t game_version = 0) const;

    /// Read a blob, verifying the container magic and CRC. A plain legacy
    /// file (no F2SV magic) is returned as version 0 with a computed CRC, so
    /// older saves keep loading.
    bool LoadBlob(std::string_view slot, std::string_view dir,
                  std::string &payload, BlobInfo &info) const;

    /// Read only the metadata; the payload CRC is verified for v2 blobs.
    bool ReadBlobInfo(std::string_view slot, std::string_view dir,
                      BlobInfo &info) const;

    /// Slot names present in `dir` (both key/value `.json` and `.sav` blobs),
    /// sorted and de-duplicated.
    [[nodiscard]] static std::vector<std::string> ListSlots(std::string_view dir);
    /// Delete `<slot>.json` and `<slot>.sav`; true when nothing remains.
    static bool DeleteSlot(std::string_view slot, std::string_view dir);
    /// Rename both container files of a slot; false if the destination exists
    /// or a rename fails.
    static bool RenameSlot(std::string_view from, std::string_view to,
                           std::string_view dir);

private:
    struct Value {
        bool is_string = false;
        double number = 0.0;
        std::string text;
    };
    std::unordered_map<std::string, Value> values_;
};

} // namespace fake2d

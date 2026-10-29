#include "fake2d/save_store.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace fake2d {

namespace {
constexpr const char *kSlotExt = ".json";
constexpr const char *kBlobExt = ".sav";
constexpr char kBlobMagic[4] = {'F', '2', 'S', 'V'};
constexpr std::uint32_t kBlobContainerVersion = 1;

std::uint32_t Crc32(const std::string &data) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        ready = true;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char byte : data) {
        crc = table[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

void AppendU32(std::string &out, std::uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFFu));
    out.push_back(static_cast<char>((value >> 8) & 0xFFu));
    out.push_back(static_cast<char>((value >> 16) & 0xFFu));
    out.push_back(static_cast<char>((value >> 24) & 0xFFu));
}

std::uint32_t ReadU32(const std::string &data, std::size_t offset) {
    if (offset + 4 > data.size()) {
        return 0;
    }
    return static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 3])) << 24);
}

std::string SlotFile(std::string_view dir, std::string_view slot,
                     std::string_view ext) {
    return (std::filesystem::path(std::string(dir)) /
            (std::string(slot) + std::string(ext))).string();
}

bool ReadWholeFile(const std::string &path, std::string &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

/// Decode one blob file (modern F2SV container or a legacy bare payload).
bool ParseBlob(std::string raw, std::string &payload, SaveStore::BlobInfo &info) {
    info = SaveStore::BlobInfo{};
    if (raw.size() >= 16 && std::memcmp(raw.data(), kBlobMagic, 4) == 0) {
        if (ReadU32(raw, 4) != kBlobContainerVersion) {
            return false;
        }
        const std::uint32_t version = ReadU32(raw, 8);
        const std::uint32_t header_len = ReadU32(raw, 12);
        const std::size_t payload_offset = 16 + static_cast<std::size_t>(header_len) + 4;
        if (header_len > 1024 * 1024 || payload_offset > raw.size()) {
            return false;
        }
        const std::uint32_t crc = ReadU32(raw, 16 + header_len);
        const std::string body = raw.substr(payload_offset);
        if (Crc32(body) != crc) {
            return false;
        }
        payload = body;
        info.game_version = version;
        info.header = raw.substr(16, header_len);
        info.payload_bytes = static_cast<std::uint32_t>(body.size());
        info.payload_crc = crc;
        return true;
    }
    // Legacy: the file itself is the payload.
    payload = std::move(raw);
    info.game_version = 0;
    info.payload_bytes = static_cast<std::uint32_t>(payload.size());
    info.payload_crc = Crc32(payload);
    return true;
}

} // namespace

bool SaveStore::ValidSlotName(std::string_view slot) {
    if (slot.empty() || slot.size() > kMaxSlotNameLen) {
        return false;
    }
    for (char ch : slot) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool SaveStore::LoadSlot(std::string_view slot, std::string_view dir) {
    if (!ValidSlotName(slot)) {
        return false;
    }
    const auto path = (std::filesystem::path(std::string(dir)) /
                       (std::string(slot) + kSlotExt));
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        values_.clear();
        return true;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    nlohmann::json doc;
    try {
        in >> doc;
    } catch (const std::exception &) {
        return false;
    }
    const auto values = doc.find("values");
    if (values == doc.end() || !values->is_object()) {
        return false;
    }
    std::unordered_map<std::string, Value> loaded;
    for (auto it = values->begin(); it != values->end(); ++it) {
        Value v;
        if (it->is_object()) {
            if (it->contains("n") && (*it)["n"].is_number()) {
                v.is_string = false;
                v.number = (*it)["n"].get<double>();
            } else if (it->contains("s") && (*it)["s"].is_string()) {
                v.is_string = true;
                v.text = (*it)["s"].get<std::string>();
            } else {
                continue;
            }
        } else if (it->is_number()) {
            v.number = it->get<double>();
        } else if (it->is_string()) {
            v.is_string = true;
            v.text = it->get<std::string>();
        } else {
            continue;
        }
        loaded.emplace(it.key(), std::move(v));
    }
    values_ = std::move(loaded);
    return true;
}

bool SaveStore::SaveSlot(std::string_view slot, std::string_view dir) const {
    if (!ValidSlotName(slot)) {
        return false;
    }
    std::error_code ec;
    std::filesystem::path dir_path{std::string(dir)};
    std::filesystem::create_directories(dir_path, ec); // best effort

    const auto path = dir_path / (std::string(slot) + kSlotExt);
    const auto tmp = dir_path / (std::string(slot) + ".tmp");

    nlohmann::json values = nlohmann::json::object();
    for (const auto &[key, v] : values_) {
        if (v.is_string) {
            values[key] = {{"s", v.text}};
        } else {
            values[key] = {{"n", v.number}};
        }
    }
    nlohmann::json doc = {{"version", 1}, {"values", std::move(values)}};

    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << doc.dump(2);
    out.flush();
    if (!out.good()) {
        return false;
    }
    out.close();
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

void SaveStore::SetNumber(std::string_view key, double value) {
    auto &v = values_[std::string(key)];
    v.is_string = false;
    v.number = value;
}

void SaveStore::SetString(std::string_view key, std::string_view value) {
    auto &v = values_[std::string(key)];
    v.is_string = true;
    v.text = std::string(value);
}

double SaveStore::GetNumber(std::string_view key, double fallback) const {
    auto it = values_.find(std::string(key));
    if (it == values_.end() || it->second.is_string) {
        return fallback;
    }
    return it->second.number;
}

std::string SaveStore::GetString(std::string_view key,
                                 std::string_view fallback) const {
    auto it = values_.find(std::string(key));
    if (it == values_.end() || !it->second.is_string) {
        return std::string(fallback);
    }
    return it->second.text;
}

bool SaveStore::Has(std::string_view key) const {
    return values_.find(std::string(key)) != values_.end();
}

void SaveStore::Erase(std::string_view key) {
    values_.erase(std::string(key));
}

bool SaveStore::SaveBlob(std::string_view slot, std::string_view dir,
                         std::string_view payload, std::string_view header,
                         std::uint32_t game_version) const {
    if (!ValidSlotName(slot) || header.size() > 1024 * 1024) {
        return false;
    }
    std::error_code ec;
    const std::filesystem::path dir_path{std::string(dir)};
    std::filesystem::create_directories(dir_path, ec);
    const std::string path = SlotFile(dir, slot, kBlobExt);
    const std::string tmp = path + ".tmp";

    std::string buf;
    buf.reserve(16 + header.size() + 4 + payload.size());
    buf.append(kBlobMagic, 4);
    AppendU32(buf, kBlobContainerVersion);
    AppendU32(buf, game_version);
    AppendU32(buf, static_cast<std::uint32_t>(header.size()));
    buf.append(header);
    AppendU32(buf, Crc32(std::string(payload)));
    buf.append(payload);

    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    out.flush();
    if (!out.good()) {
        out.close();
        std::filesystem::remove(tmp, ec);
        return false;
    }
    out.close();
    // Keep the previous save as `.bak`: a torn write or a later corrupt file
    // still leaves the player with the state before this save.
    std::error_code ec_backup;
    if (std::filesystem::exists(path, ec_backup)) {
        const std::string backup = path + ".bak";
        std::filesystem::remove(backup, ec_backup);
        std::filesystem::rename(path, backup, ec_backup);
        if (ec_backup) {
            // No backup could be kept (locked file, cross-device, ...); drop
            // the primary so the new save can still replace it.
            std::error_code ec_remove;
            std::filesystem::remove(path, ec_remove);
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool SaveStore::LoadBlob(std::string_view slot, std::string_view dir,
                         std::string &payload, BlobInfo &info) const {
    if (!ValidSlotName(slot)) {
        return false;
    }
    std::string raw;
    const std::string path = SlotFile(dir, slot, kBlobExt);
    if (ReadWholeFile(path, raw) && ParseBlob(std::move(raw), payload, info)) {
        return true;
    }
    if (ReadWholeFile(path + ".bak", raw) &&
        ParseBlob(std::move(raw), payload, info)) {
        info.restored_from_backup = true;
        return true;
    }
    return false;
}

bool SaveStore::ReadBlobInfo(std::string_view slot, std::string_view dir,
                             BlobInfo &info) const {
    std::string payload;
    return LoadBlob(slot, dir, payload, info);
}

std::vector<std::string> SaveStore::ListSlots(std::string_view dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(std::string(dir), ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string ext = entry.path().extension().string();
        if (ext != kSlotExt && ext != kBlobExt) {
            continue;
        }
        const std::string stem = entry.path().stem().string();
        if (ValidSlotName(stem)) {
            names.push_back(stem);
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

bool SaveStore::DeleteSlot(std::string_view slot, std::string_view dir) {
    if (!ValidSlotName(slot)) {
        return false;
    }
    std::error_code ec;
    std::filesystem::remove(SlotFile(dir, slot, kSlotExt), ec);
    ec.clear();
    std::filesystem::remove(SlotFile(dir, slot, kBlobExt), ec);
    ec.clear();
    std::filesystem::remove(SlotFile(dir, slot, ".sav.bak"), ec);
    return !std::filesystem::exists(SlotFile(dir, slot, kSlotExt)) &&
           !std::filesystem::exists(SlotFile(dir, slot, kBlobExt));
}

bool SaveStore::RenameSlot(std::string_view from, std::string_view to,
                           std::string_view dir) {
    if (!ValidSlotName(from) || !ValidSlotName(to) || from == to) {
        return false;
    }
    for (const char *ext : {kSlotExt, kBlobExt, ".sav.bak"}) {
        const std::string src = SlotFile(dir, from, ext);
        const std::string dst = SlotFile(dir, to, ext);
        std::error_code ec;
        if (!std::filesystem::exists(src, ec)) {
            continue;
        }
        if (std::filesystem::exists(dst, ec)) {
            return false;
        }
        std::filesystem::rename(src, dst, ec);
        if (ec) {
            return false;
        }
    }
    return true;
}

} // namespace fake2d

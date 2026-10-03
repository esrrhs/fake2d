#include "fake2d/save_store.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fake2d {

namespace {
constexpr const char *kSlotExt = ".json";
}

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

} // namespace fake2d

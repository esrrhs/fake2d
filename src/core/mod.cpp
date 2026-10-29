#include "fake2d/mod.h"

#include "fake2d/i18n.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace fake2d {

namespace {

bool ReadManifest(const std::filesystem::path &dir, ModManager::ModInfo &info) {
    std::ifstream in(dir / "mod.json");
    if (!in) {
        return false;
    }
    nlohmann::json doc;
    try {
        in >> doc;
    } catch (const std::exception &) {
        return false;
    }
    if (!doc.is_object()) {
        return false;
    }
    info.path = dir.string();
    info.name = doc.value("name", dir.filename().string());
    info.version = doc.value("version", std::string{});
    info.enabled = doc.value("enabled", true);
    info.load_order = doc.value("load_order", 0);
    return !info.name.empty();
}

} // namespace

void ModManager::Scan(const std::vector<std::string> &dirs) {
    Clear();
    ScanMore(dirs);
}

void ModManager::ScanMore(const std::vector<std::string> &dirs) {
    for (const std::string &root : dirs) {
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec || !entry.is_directory(ec)) {
                continue;
            }
            ModInfo info;
            if (ReadManifest(entry.path(), info)) {
                mods_.push_back(std::move(info));
            }
        }
    }
    SortMods();
}

void ModManager::SortMods() {
    // Stable so equal load_order keeps scan order (root order then name).
    std::stable_sort(mods_.begin(), mods_.end(),
                     [](const ModInfo &a, const ModInfo &b) {
                         return a.load_order < b.load_order;
                     });
}

void ModManager::Clear() {
    mods_.clear();
}

std::size_t ModManager::ActiveCount() const {
    return static_cast<std::size_t>(std::count_if(
        mods_.begin(), mods_.end(),
        [](const ModInfo &mod) { return mod.enabled; }));
}

std::string ModManager::ActiveName(std::size_t index) const {
    for (const ModInfo &mod : mods_) {
        if (!mod.enabled) {
            continue;
        }
        if (index == 0) {
            return mod.name;
        }
        --index;
    }
    return {};
}

bool ModManager::Enabled(std::string_view name) const {
    for (const ModInfo &mod : mods_) {
        if (mod.name == name) {
            return mod.enabled;
        }
    }
    return false;
}

bool ModManager::SetEnabled(std::string_view name, bool enabled) {
    for (ModInfo &mod : mods_) {
        if (mod.name == name) {
            mod.enabled = enabled;
            return true;
        }
    }
    return false;
}

std::string ModManager::Resolve(std::string_view relative) const {
    std::error_code ec;
    // Later load order wins, so walk backwards.
    for (auto it = mods_.rbegin(); it != mods_.rend(); ++it) {
        if (!it->enabled) {
            continue;
        }
        const std::filesystem::path candidate =
            std::filesystem::path(it->path) / std::string(relative);
        if (std::filesystem::exists(candidate, ec)) {
            return candidate.string();
        }
    }
    if (!base_dir_.empty()) {
        return (std::filesystem::path(base_dir_) / std::string(relative)).string();
    }
    return {};
}

int ModManager::LoadLanguages(I18n &i18n) const {
    int parsed = 0;
    for (const ModInfo &mod : mods_) {
        if (!mod.enabled) {
            continue;
        }
        std::error_code ec;
        const std::filesystem::path lang_dir =
            std::filesystem::path(mod.path) / "lang";
        for (const auto &entry : std::filesystem::directory_iterator(lang_dir, ec)) {
            if (ec || !entry.is_regular_file(ec)) {
                continue;
            }
            if (entry.path().extension() != ".tsv") {
                continue;
            }
            parsed += i18n.LoadFile(entry.path().string(),
                                    entry.path().stem().string());
        }
    }
    return parsed;
}

std::string ModManager::Report() const {
    std::string out;
    for (const ModInfo &mod : mods_) {
        if (!out.empty()) {
            out += '\n';
        }
        out += mod.name;
        if (!mod.version.empty()) {
            out += ' ';
            out += mod.version;
        }
        out += mod.enabled ? "  [on]" : "  [off]";
        out += "  order ";
        out += std::to_string(mod.load_order);
    }
    return out;
}

} // namespace fake2d

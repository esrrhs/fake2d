#include "fake2d/achievements.h"

#include "fake2d/app_paths.h"
#include "fake2d/steam.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fake2d {

void Achievements::SetPath(std::string path) {
    path_ = std::move(path);
    loaded_ = false;
    path_touched_ = true;
    unlocked_.clear();
}

const std::string &Achievements::Path() {
    if (!path_touched_) {
        path_ = AppPaths::UserDir() + "/achievements.txt";
        path_touched_ = true;
    }
    return path_;
}

void Achievements::LoadIfNeeded() {
    if (loaded_) {
        return;
    }
    loaded_ = true;
    const std::string &path = Path();
    std::ifstream in(path);
    if (!in) {
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            unlocked_.insert(line);
        }
    }
}

void Achievements::Save() {
    const std::string &path = Path();
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return;
    }
    std::vector<std::string> ids(unlocked_.begin(), unlocked_.end());
    std::sort(ids.begin(), ids.end());
    for (const std::string &id : ids) {
        out << id << '\n';
    }
}

bool Achievements::Unlock(std::string_view id) {
    if (id.empty()) {
        return false;
    }
    LoadIfNeeded();
    const auto inserted = unlocked_.insert(std::string(id));
    if (inserted.second) {
        Save();
    }
    // Forward even when the id was already local: Steam may have missed the
    // unlock while offline, and repeated SetAchievement calls are idempotent.
    if (steam::Available()) {
        steam::UnlockAchievement(id);
    }
    return inserted.second;
}

bool Achievements::IsUnlocked(std::string_view id) {
    LoadIfNeeded();
    return unlocked_.find(std::string(id)) != unlocked_.end();
}

std::size_t Achievements::Count() {
    LoadIfNeeded();
    return unlocked_.size();
}

std::string Achievements::IdAt(std::size_t index) {
    LoadIfNeeded();
    std::vector<std::string> ids(unlocked_.begin(), unlocked_.end());
    std::sort(ids.begin(), ids.end());
    return index < ids.size() ? ids[index] : std::string{};
}

void Achievements::SyncToSteam() {
    LoadIfNeeded();
    if (!steam::Available()) {
        return;
    }
    for (const std::string &id : unlocked_) {
        steam::UnlockAchievement(id);
    }
}

void Achievements::Clear() {
    LoadIfNeeded();
    unlocked_.clear();
    Save();
}

} // namespace fake2d

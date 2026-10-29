#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fake2d {

/// Local achievement mirror + Steam sync.
///
/// Unlocks are always recorded locally (a plain one-ID-per-line text file) so
/// the game works offline or without the Steamworks SDK; SyncToSteam() pushes
/// everything that is not confirmed yet the next time Steam is available.
/// The storage path defaults to `AppPaths::UserDir()/achievements.txt` and is
/// only touched on first use.
class Achievements {
public:
    void SetPath(std::string path);
    [[nodiscard]] const std::string &Path();

    /// Record an unlock locally and forward it to Steam when available.
    /// Returns true only the first time the id is unlocked.
    bool Unlock(std::string_view id);
    /// Local state (loads the mirror on first call).
    [[nodiscard]] bool IsUnlocked(std::string_view id);
    [[nodiscard]] std::size_t Count();
    /// Id at a stable index, sorted ("" when out of range).
    [[nodiscard]] std::string IdAt(std::size_t index);

    /// Push every locally unlocked id to Steam and flush stats.
    void SyncToSteam();
    /// Forget the in-memory set (the mirror file is rewritten empty).
    void Clear();

private:
    void LoadIfNeeded();
    void Save();

    std::string path_;
    bool loaded_ = false;
    bool path_touched_ = false;
    std::unordered_set<std::string> unlocked_;
};

} // namespace fake2d

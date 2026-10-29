#pragma once

#include <string>
#include <string_view>

namespace fake2d {

/// Cross-platform filesystem conventions for games.
///
/// Resource root: where `assets/`, `scripts/`, `maps/` and `lang/` live.
/// User dir: writable per-user storage for saves, logs, settings and mods,
/// resolved per platform and overridable so packaged/portable builds and
/// tests never touch the real user profile.
class AppPaths {
public:
    /// Override the resource root (otherwise auto-detected: `FAKE2D_ROOT`,
    /// then the working directory containing `assets/`, then the executable
    /// directory / macOS bundle Resources).
    static void SetResourceRoot(std::string dir);
    [[nodiscard]] static const std::string &ResourceRoot();
    /// Join a relative path onto the resource root.
    [[nodiscard]] static std::string Resolve(std::string_view relative);

    /// Override the user data directory (otherwise `FAKE2D_USER_DIR`, then
    /// the platform default). The directory is created on first use.
    static void SetUserDir(std::string dir);
    [[nodiscard]] static const std::string &UserDir();

    [[nodiscard]] static std::string SavesDir();   // <user>/saves
    [[nodiscard]] static std::string LogsDir();    // <user>/logs
    [[nodiscard]] static std::string ConfigPath(); // <user>/settings.json
    [[nodiscard]] static std::string UserModsDir();// <user>/mods

    /// Best-effort recursive directory creation; true when it exists.
    static bool EnsureDir(const std::string &dir);

    /// Directory containing the running executable (empty when unknown).
    [[nodiscard]] static std::string ExecutableDir();

private:
    [[nodiscard]] static std::string DetectResourceRoot();
    [[nodiscard]] static std::string DetectUserDir();
};

} // namespace fake2d

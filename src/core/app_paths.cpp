#include "fake2d/app_paths.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace fake2d {

namespace {

std::string PathToUtf8(const std::filesystem::path &path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

std::string GetEnv(const char *name) {
#if defined(_WIN32)
    char *buffer = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) == 0 && buffer != nullptr) {
        std::string value(buffer);
        std::free(buffer);
        return value;
    }
    return {};
#else
    const char *value = std::getenv(name);
    return value != nullptr ? value : std::string();
#endif
}

bool HasAssets(const std::filesystem::path &dir) {
    std::error_code ec;
    return std::filesystem::is_directory(dir / "assets", ec);
}

std::string &RootStorage() {
    static std::string root;
    return root;
}

std::string &UserStorage() {
    static std::string dir;
    return dir;
}

} // namespace

void AppPaths::SetResourceRoot(std::string dir) {
    RootStorage() = std::move(dir);
}

const std::string &AppPaths::ResourceRoot() {
    if (RootStorage().empty()) {
        RootStorage() = DetectResourceRoot();
    }
    return RootStorage();
}

std::string AppPaths::Resolve(std::string_view relative) {
    const std::filesystem::path base(ResourceRoot());
    return PathToUtf8(base / std::filesystem::path(std::string(relative)));
}

void AppPaths::SetUserDir(std::string dir) {
    UserStorage() = std::move(dir);
    if (!UserStorage().empty()) {
        EnsureDir(UserStorage());
    }
}

const std::string &AppPaths::UserDir() {
    if (UserStorage().empty()) {
        UserStorage() = DetectUserDir();
        EnsureDir(UserStorage());
    }
    return UserStorage();
}

std::string AppPaths::SavesDir() {
    return PathToUtf8(std::filesystem::path(UserDir()) / "saves");
}

std::string AppPaths::LogsDir() {
    return PathToUtf8(std::filesystem::path(UserDir()) / "logs");
}

std::string AppPaths::ConfigPath() {
    return PathToUtf8(std::filesystem::path(UserDir()) / "settings.json");
}

std::string AppPaths::UserModsDir() {
    return PathToUtf8(std::filesystem::path(UserDir()) / "mods");
}

bool AppPaths::EnsureDir(const std::string &dir) {
    if (dir.empty()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(dir), ec);
    return std::filesystem::is_directory(std::filesystem::path(dir), ec);
}

std::string AppPaths::ExecutableDir() {
#if defined(__APPLE__)
    char buffer[4096];
    std::uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        std::error_code ec;
        const auto canonical = std::filesystem::weakly_canonical(buffer, ec);
        if (!ec) {
            return PathToUtf8(canonical.parent_path());
        }
    }
#elif defined(_WIN32)
    wchar_t buffer[4096];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, 4096);
    if (length > 0 && length < 4096) {
        return PathToUtf8(std::filesystem::path(buffer).parent_path());
    }
#elif defined(__linux__)
    char buffer[4096];
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length > 0) {
        buffer[length] = '\0';
        return PathToUtf8(std::filesystem::path(buffer).parent_path());
    }
#endif
    return {};
}

std::string AppPaths::DetectResourceRoot() {
    if (const std::string env = GetEnv("FAKE2D_ROOT"); !env.empty()) {
        return PathToUtf8(std::filesystem::path(env));
    }

    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec && HasAssets(cwd)) {
        return PathToUtf8(cwd);
    }

    const std::string exe_dir = ExecutableDir();
    if (!exe_dir.empty()) {
        const auto exe = std::filesystem::path(exe_dir);
#if defined(__APPLE__)
        // A packaged .app keeps data in Contents/Resources.
        for (const auto &candidate : {exe, exe.parent_path(), exe.parent_path().parent_path()}) {
            const auto resources = candidate / "Resources";
            if (HasAssets(resources)) {
                return PathToUtf8(resources);
            }
            if (HasAssets(candidate)) {
                return PathToUtf8(candidate);
            }
        }
#else
        if (HasAssets(exe)) {
            return exe_dir;
        }
#endif
        return exe_dir;
    }
    return "."; // last resort: working directory
}

std::string AppPaths::DetectUserDir() {
    if (const std::string env = GetEnv("FAKE2D_USER_DIR"); !env.empty()) {
        return PathToUtf8(std::filesystem::path(env));
    }
#if defined(_WIN32)
    if (const std::string appdata = GetEnv("APPDATA"); !appdata.empty()) {
        return PathToUtf8(std::filesystem::path(appdata) / "Fake2D");
    }
#elif defined(__APPLE__)
    if (const std::string home = GetEnv("HOME"); !home.empty()) {
        return PathToUtf8(std::filesystem::path(home) / "Library" /
                          "Application Support" / "Fake2D");
    }
#else
    if (const std::string xdg = GetEnv("XDG_DATA_HOME"); !xdg.empty()) {
        return PathToUtf8(std::filesystem::path(xdg) / "fake2d");
    }
    if (const std::string home = GetEnv("HOME"); !home.empty()) {
        return PathToUtf8(std::filesystem::path(home) / ".local" / "share" /
                          "fake2d");
    }
#endif
    std::error_code ec;
    const auto temp = std::filesystem::temp_directory_path(ec);
    if (!ec) {
        return PathToUtf8(temp / "fake2d-user");
    }
    return "fake2d-user";
}

} // namespace fake2d

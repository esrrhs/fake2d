#include "fake2d/log.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <cstdlib>
#include <execinfo.h>
#include <unistd.h>
#endif

namespace fake2d::log {

namespace {

std::FILE *g_file = nullptr;
std::string g_path;
Config g_config;
std::mutex g_mutex;
std::atomic<bool> g_active{false};

void Stamp(char *buffer, std::size_t size, const char *format) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::strftime(buffer, size, format, &local);
}

std::string RotatedName(const std::filesystem::path &path) {
    const std::string stem = path.stem().string();
    const std::string ext = path.extension().string();
    return (path.parent_path() / (stem + ".prev" + ext)).string();
}

void WriteCrashReport(const char *reason) {
    static std::atomic<bool> written{false};
    if (written.exchange(true)) {
        return;
    }
    const std::string dir = g_config.dir.empty() ? std::string(".") : g_config.dir;
    char stamp[64];
    Stamp(stamp, sizeof stamp, "%Y%m%d-%H%M%S");
    const std::string report = dir + "/crash-" + stamp + ".txt";

    std::FILE *file = std::fopen(report.c_str(), "w");
    if (file == nullptr) {
        return;
    }
    std::fprintf(file, "%s %s crash report\nreason: %s\nlog: %s\n\nbacktrace:\n",
                 g_config.app_name.c_str(), g_config.version.c_str(), reason,
                 g_path.c_str());
#if defined(_WIN32)
    void *frames[64];
    const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        std::fprintf(file, "  #%u %p\n", static_cast<unsigned>(i), frames[i]);
    }
#else
    void *frames[64];
    const int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, fileno(file));
#endif
    std::fclose(file);
}

#if defined(_WIN32)
LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS *info) {
    char reason[128];
    std::snprintf(reason, sizeof reason, "unhandled exception 0x%08lx",
                  static_cast<unsigned long>(
                      info != nullptr ? info->ExceptionRecord->ExceptionCode : 0));
    WriteCrashReport(reason);
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
void FatalSignal(int sig) {
    const char *reason = "fatal signal";
    switch (sig) {
        case SIGSEGV: reason = "SIGSEGV (segmentation fault)"; break;
        case SIGABRT: reason = "SIGABRT (abort)"; break;
        case SIGFPE: reason = "SIGFPE (arithmetic error)"; break;
        case SIGILL: reason = "SIGILL (illegal instruction)"; break;
        case SIGBUS: reason = "SIGBUS (bus error)"; break;
        default: break;
    }
    WriteCrashReport(reason);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
#endif

void InstallCrashHandler() {
#if defined(_WIN32)
    SetUnhandledExceptionFilter(UnhandledFilter);
#else
    for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
        std::signal(sig, FatalSignal);
    }
#endif
}

} // namespace

bool Init(const Config &config) {
    Shutdown();
    g_config = config;
    g_path.clear();

    std::error_code ec;
    if (!config.dir.empty()) {
        std::filesystem::create_directories(config.dir, ec);
        const std::filesystem::path path =
            std::filesystem::path(config.dir) / config.filename;
        if (config.rotate && std::filesystem::exists(path, ec)) {
            std::filesystem::remove(RotatedName(path), ec);
            std::filesystem::rename(path, RotatedName(path), ec);
        }
        g_file = std::fopen(path.string().c_str(), "w");
        if (g_file != nullptr) {
            g_path = path.string();
        }
    }

    g_active = true;
    char stamp[64];
    Stamp(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S");
    Info(std::string("session start ") + stamp + " · " + config.app_name + " " +
         config.version);
    if (config.crash_reports) {
        InstallCrashHandler();
    }
    return g_file != nullptr || config.dir.empty();
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file != nullptr) {
        std::fclose(g_file);
        g_file = nullptr;
    }
    g_path.clear();
    g_active = false;
}

bool Active() { return g_active.load(); }

const std::string &Path() { return g_path; }

void Write(std::string_view level, std::string_view message) {
    char stamp[32];
    Stamp(stamp, sizeof stamp, "%H:%M:%S");
    std::lock_guard<std::mutex> lock(g_mutex);
    const bool to_stderr = g_config.echo_stderr || g_file == nullptr;
    if (to_stderr) {
        std::fprintf(stderr, "[%s] [%.*s] %.*s\n", stamp,
                     static_cast<int>(level.size()), level.data(),
                     static_cast<int>(message.size()), message.data());
    }
    if (g_file != nullptr) {
        std::fprintf(g_file, "%s [%.*s] %.*s\n", stamp,
                     static_cast<int>(level.size()), level.data(),
                     static_cast<int>(message.size()), message.data());
        std::fflush(g_file);
    }
}

void Info(std::string_view message) { Write("info", message); }
void Warn(std::string_view message) { Write("warn", message); }
void Error(std::string_view message) { Write("error", message); }

} // namespace fake2d::log

#include "fake2d/log.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <mutex>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <execinfo.h>
#include <sys/wait.h>
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
    if (file != nullptr) {
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
        std::fflush(file);
        backtrace_symbols_fd(frames, count, fileno(file));
#endif
        std::fclose(file);
    }
    Error(std::string("[fatal] ") + reason + " — report: " + report);
    if (!g_config.crash_dialog_title.empty()) {
        std::string msg;
        if (!g_config.crash_dialog_body_prefix.empty()) msg += g_config.crash_dialog_body_prefix + "\n\n";
        msg += reason;
        msg += "\n\nCrash report:\n" + report;
        if (!g_config.crash_dialog_body_suffix.empty()) msg += "\n\n" + g_config.crash_dialog_body_suffix;
        ErrorDialog(g_config.crash_dialog_title, msg);
    }
}

#if defined(_WIN32)
void WriteMiniDump(EXCEPTION_POINTERS *info) {
    if (!g_config.windows_minidump || !info) return;
    HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
    if (!dbghelp) return;
    struct MiniDumpInfo { DWORD ThreadId; PEXCEPTION_POINTERS ExceptionPointers; BOOL ClientPointers; };
    using Fn = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, DWORD, MiniDumpInfo *, void *, void *);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    if (fn) {
        const std::string dir = g_config.dir.empty() ? std::string(".") : g_config.dir;
        char stamp[64];
        Stamp(stamp, sizeof stamp, "%Y%m%d-%H%M%S");
        const std::string dmp = dir + "/crash-" + stamp + ".dmp";
        HANDLE h = CreateFileA(dmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            MiniDumpInfo mei{GetCurrentThreadId(), info, FALSE};
            fn(GetCurrentProcess(), GetCurrentProcessId(), h, 0 /* MiniDumpNormal */, &mei, nullptr, nullptr);
            CloseHandle(h);
        }
    }
    FreeLibrary(dbghelp);
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS *info) {
    WriteMiniDump(info);
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
    if (!g_config.disable_crash_env.empty() && std::getenv(g_config.disable_crash_env.c_str())) {
        return;
    }
#if defined(_WIN32)
    SetUnhandledExceptionFilter(UnhandledFilter);
#else
    for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
        std::signal(sig, FatalSignal);
    }
#endif
    if (g_config.terminate_handler) {
        std::set_terminate([] {
            std::string what = "std::terminate (uncaught exception)";
            if (auto e = std::current_exception()) {
                try { std::rethrow_exception(e); }
                catch (const std::exception &x) { what += ": "; what += x.what(); }
                catch (...) {}
            }
            WriteCrashReport(what.c_str());
            std::_Exit(3);
        });
    }
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

void ErrorDialog(std::string_view title, std::string_view message) {
    Error(std::string(title) + ": " + std::string(message));
    if (std::getenv("FAKE2D_NO_DIALOG")) return;
    if (!g_config.no_dialog_env.empty() && std::getenv(g_config.no_dialog_env.c_str())) return;
    const std::string t(title), m(message);
#if defined(_WIN32)
    auto wide = [](const std::string &s) {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(n > 0 ? n : 1, 0);
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
        return w;
    };
    MessageBoxW(nullptr, wide(m).c_str(), wide(t).c_str(), MB_OK | MB_ICONERROR);
#else
    [[maybe_unused]] auto esc = [](const std::string &s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') o += '\\';
            o += c;
        }
        return o;
    };
    pid_t pid = fork();
    if (pid == 0) {
#if defined(__APPLE__)
        std::string script = "display alert \"" + esc(t) + "\" message \"" + esc(m) + "\" as critical";
        execlp("osascript", "osascript", "-e", script.c_str(), static_cast<char *>(nullptr));
#else
        execlp("zenity", "zenity", "--error", "--no-wrap", ("--title=" + t).c_str(), ("--text=" + m).c_str(), static_cast<char *>(nullptr));
        execlp("kdialog", "kdialog", "--title", t.c_str(), "--error", m.c_str(), static_cast<char *>(nullptr));
        execlp("xmessage", "xmessage", "-center", (t + "\n\n" + m).c_str(), static_cast<char *>(nullptr));
#endif
        _exit(127);
    } else if (pid > 0) {
        int st = 0;
        waitpid(pid, &st, 0);
    }
#endif
}

} // namespace fake2d::log

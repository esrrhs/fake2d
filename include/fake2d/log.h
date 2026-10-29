#pragma once

#include <string>
#include <string_view>

namespace fake2d {

/// Optional file logger with rotation and crash reports.
///
/// The engine itself never opens files; a host opts in with Init(), usually
/// pointing at `AppPaths::LogsDir()`. Messages always go to stderr (unless
/// disabled) and, when a directory is configured, to `<dir>/<filename>` with a
/// timestamp per line. Init rotates an existing log to `game.prev.log`.
///
/// With crash reports enabled, a fatal signal/unhandled exception writes
/// `crash-<timestamp>.txt` containing the reason and a native backtrace, then
/// lets the process die normally (so debuggers and CI still see the crash).
namespace log {

struct Config {
    /// Directory for the log and crash reports; empty = stderr only.
    std::string dir;
    std::string filename = "game.log";
    /// Mirror every line to stderr.
    bool echo_stderr = true;
    /// Move an existing `<filename>` to `<stem>.prev<ext>` on Init.
    bool rotate = true;
    /// Install fatal-signal handlers that write a crash report.
    bool crash_reports = true;
    /// Reported in the session header and crash reports.
    std::string app_name = "fake2d";
    std::string version;
};

/// Start logging. Returns false when the file could not be opened (stderr
/// logging still works).
bool Init(const Config &config);
void Shutdown();
[[nodiscard]] bool Active();
/// Absolute/relative path of the open log file ("" when stderr-only).
[[nodiscard]] const std::string &Path();

/// Core entry point (level is free-form: "info", "warn", "error", ...).
void Write(std::string_view level, std::string_view message);
void Info(std::string_view message);
void Warn(std::string_view message);
void Error(std::string_view message);

} // namespace log
} // namespace fake2d

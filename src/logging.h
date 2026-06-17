// logging.* — central, timestamped, toggleable logging.
//
// This mod is debugged blind (the user runs the game and pastes the log back),
// so logging is a first-class subsystem. It must be:
//   * cheap when enabled (no allocation on the hot path beyond formatting),
//   * a complete no-op when disabled (single bool check, returns immediately),
//   * safe to call from multiple D3D threads.
#pragma once

#include <string>

// Namespace is `logger` (not `log`) to avoid clashing with <cmath>'s ::log.
namespace logger {

// Initialise the log file. `enabled == false` makes every Write() a no-op.
// Safe to call once at DLL attach. `path` is the full path to the log file.
void Init(bool enabled, const std::string& path);

// Flush + close. Call at DLL detach.
void Shutdown();

bool Enabled();

// printf-style. Newline is appended automatically. No-op when disabled.
void Write(const char* fmt, ...);

}  // namespace logger

// Convenience macro: avoids even evaluating the format args when logging is off.
#define LOG(...)                       \
  do {                                 \
    if (::logger::Enabled())           \
      ::logger::Write(__VA_ARGS__);    \
  } while (0)

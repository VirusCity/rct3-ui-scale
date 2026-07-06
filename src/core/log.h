// core/log.h — required logging (CLAUDE.md): discovery attempts, candidates
// found/rejected with reasons, and the final selected method.
// Always available, low overhead, toggleable via [Logging] Enabled in the INI.
#pragma once

namespace logx {

// Opens (appends to) the log file. Safe to call once from DllMain attach.
bool Init(bool enabled, const char* path);
void Shutdown();
bool Enabled();

// printf-style; adds timestamp + newline. Thread-safe. No-op when disabled.
void Write(const char* fmt, ...);

}  // namespace logx

#define LOG(...) ::logx::Write(__VA_ARGS__)

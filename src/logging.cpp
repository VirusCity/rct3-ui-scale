#include "logging.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace logger {
namespace {

bool             g_enabled = false;
HANDLE           g_file = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_lock;
bool             g_lockInit = false;

void Timestamp(char* out, size_t n) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  _snprintf_s(out, n, _TRUNCATE, "%02d:%02d:%02d.%03d", t.wHour, t.wMinute,
              t.wSecond, t.wMilliseconds);
}

}  // namespace

void Init(bool enabled, const std::string& path) {
  g_enabled = enabled;
  if (!enabled) return;

  if (!g_lockInit) {
    InitializeCriticalSection(&g_lock);
    g_lockInit = true;
  }

  // Truncate on each launch so the log reflects a single session.
  g_file = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (g_file == INVALID_HANDLE_VALUE) {
    g_enabled = false;  // can't log — degrade silently rather than crash.
    return;
  }

  SYSTEMTIME t;
  GetLocalTime(&t);
  Write("=== RCT3 UI Scale log started %04d-%02d-%02d %02d:%02d:%02d ===",
        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

void Shutdown() {
  if (g_file != INVALID_HANDLE_VALUE) {
    Write("=== log closed ===");
    CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
  }
  g_enabled = false;
  if (g_lockInit) {
    DeleteCriticalSection(&g_lock);
    g_lockInit = false;
  }
}

bool Enabled() { return g_enabled; }

void Write(const char* fmt, ...) {
  if (!g_enabled || g_file == INVALID_HANDLE_VALUE) return;

  char ts[16];
  Timestamp(ts, sizeof(ts));

  char body[1024];
  va_list ap;
  va_start(ap, fmt);
  _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, ap);
  va_end(ap);

  char line[1100];
  int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "[%s] %s\r\n", ts, body);
  if (n <= 0) return;

  EnterCriticalSection(&g_lock);
  DWORD written = 0;
  WriteFile(g_file, line, static_cast<DWORD>(n), &written, nullptr);
  FlushFileBuffers(g_file);  // crash-safe: the last line always lands on disk.
  LeaveCriticalSection(&g_lock);
}

}  // namespace logger

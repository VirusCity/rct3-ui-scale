#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace logx {
namespace {

CRITICAL_SECTION g_cs;
FILE* g_file = nullptr;
bool g_enabled = false;
bool g_csInit = false;

}  // namespace

bool Init(bool enabled, const char* path) {
  if (!g_csInit) {
    InitializeCriticalSection(&g_cs);
    g_csInit = true;
  }
  g_enabled = enabled;
  if (!enabled) return true;
  if (fopen_s(&g_file, path, "a") != 0) {
    g_file = nullptr;
    g_enabled = false;
    return false;
  }
  return true;
}

void Shutdown() {
  EnterCriticalSection(&g_cs);
  if (g_file) {
    fclose(g_file);
    g_file = nullptr;
  }
  g_enabled = false;
  LeaveCriticalSection(&g_cs);
}

bool Enabled() { return g_enabled; }

void Write(const char* fmt, ...) {
  if (!g_enabled || !g_file) return;

  SYSTEMTIME st;
  GetLocalTime(&st);

  EnterCriticalSection(&g_cs);
  fprintf(g_file, "[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond,
          st.wMilliseconds);
  va_list args;
  va_start(args, fmt);
  vfprintf(g_file, fmt, args);
  va_end(args);
  fputc('\n', g_file);
  fflush(g_file);
  LeaveCriticalSection(&g_cs);
}

}  // namespace logx

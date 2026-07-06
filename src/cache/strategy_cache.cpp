#include "strategy_cache.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../core/log.h"

namespace cache {
namespace {

char g_path[MAX_PATH] = {};

unsigned Fnv1a(unsigned hash, const void* data, size_t len) {
  const unsigned char* p = static_cast<const unsigned char*>(data);
  for (size_t i = 0; i < len; ++i) {
    hash ^= p[i];
    hash *= 16777619u;
  }
  return hash;
}

// Engine fingerprint from PE-header invariants of the main exe + file size.
unsigned Fingerprint() {
  const HMODULE exe = GetModuleHandleA(nullptr);
  if (!exe) return 0;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
      reinterpret_cast<const unsigned char*>(exe) + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

  unsigned h = 2166136261u;
  h = Fnv1a(h, &nt->FileHeader.TimeDateStamp, 4);
  h = Fnv1a(h, &nt->FileHeader.NumberOfSections, 2);
  h = Fnv1a(h, &nt->OptionalHeader.SizeOfImage, 4);
  h = Fnv1a(h, &nt->OptionalHeader.AddressOfEntryPoint, 4);

  char exePath[MAX_PATH];
  if (GetModuleFileNameA(exe, exePath, MAX_PATH)) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExA(exePath, GetFileExInfoStandard, &fad)) {
      h = Fnv1a(h, &fad.nFileSizeLow, 4);
      h = Fnv1a(h, &fad.nFileSizeHigh, 4);
    }
  }
  return h;
}

// True when the stored fingerprint matches the running exe.
bool FingerprintOk() {
  char buf[64] = {};
  GetPrivateProfileStringA("Cache", "Fingerprint", "", buf, sizeof(buf),
                           g_path);
  if (!buf[0]) return false;
  const unsigned stored = strtoul(buf, nullptr, 16);
  const unsigned current = Fingerprint();
  return current != 0 && stored == current;
}

void WriteFingerprint() {
  char buf[64];
  sprintf_s(buf, "%08X", Fingerprint());
  WritePrivateProfileStringA("Cache", "Fingerprint", buf, g_path);
}

}  // namespace

void Init(const char* directory) {
  sprintf_s(g_path, "%s\\d3d9_uiscale.cache", directory);
}

bool Load(Entry* out) {
  if (!g_path[0] || !out) return false;
  if (!FingerprintOk()) {
    LOG("cache: no entry / fingerprint mismatch for this exe — rediscovering");
    return false;
  }

  out->globalSlotRva =
      static_cast<unsigned>(GetPrivateProfileIntA("Cache", "GlobalSlotRVA", 0,
                                                  g_path));
  out->canvasRectOffset =
      static_cast<unsigned>(GetPrivateProfileIntA("Cache", "CanvasRectOffset",
                                                  0, g_path));
  if (!out->globalSlotRva || !out->canvasRectOffset) return false;

  LOG("cache: loaded CONFIRMED mapping {global=exe+0x%X rectOff=0x%X} — "
      "will RE-VALIDATE before trusting",
      out->globalSlotRva, out->canvasRectOffset);
  return true;
}

void StoreCatchSites(unsigned globalSlotRva, unsigned canvasRectOffset) {
  if (!g_path[0] || !globalSlotRva || !canvasRectOffset) return;
  char buf[64];
  WriteFingerprint();
  sprintf_s(buf, "%u", globalSlotRva);
  WritePrivateProfileStringA("Cache", "GlobalSlotRVA", buf, g_path);
  sprintf_s(buf, "%u", canvasRectOffset);
  WritePrivateProfileStringA("Cache", "CanvasRectOffset", buf, g_path);
  LOG("cache: stored CONFIRMED mapping (global=exe+0x%X rectOff=0x%X)",
      globalSlotRva, canvasRectOffset);
}

void InvalidateStored() {
  if (!g_path[0]) return;
  WritePrivateProfileStringA("Cache", "GlobalSlotRVA", nullptr, g_path);
  WritePrivateProfileStringA("Cache", "CanvasRectOffset", nullptr, g_path);
  WritePrivateProfileStringA("Cache", "PendingCount", nullptr, g_path);
  LOG("cache: stored mapping + pending candidates INVALIDATED");
}

int LoadPending(Entry* out, int maxOut) {
  if (!g_path[0] || !out || maxOut <= 0) return 0;
  if (!FingerprintOk()) return 0;
  int n = GetPrivateProfileIntA("Cache", "PendingCount", 0, g_path);
  if (n > maxOut) n = maxOut;
  int loaded = 0;
  for (int i = 0; i < n; ++i) {
    char key[32], buf[64] = {};
    sprintf_s(key, "Pending%d", i);
    GetPrivateProfileStringA("Cache", key, "", buf, sizeof(buf), g_path);
    unsigned rva = 0, off = 0;
    if (sscanf_s(buf, "%u,%u", &rva, &off) == 2 && rva && off) {
      out[loaded].globalSlotRva = rva;
      out[loaded].canvasRectOffset = off;
      ++loaded;
    }
  }
  if (loaded)
    LOG("cache: loaded %d PENDING candidate(s) — arming timing hooks on all "
        "so the first creation disambiguates",
        loaded);
  return loaded;
}

void StorePending(const Entry* list, int count) {
  if (!g_path[0] || !list) return;
  const int prev = GetPrivateProfileIntA("Cache", "PendingCount", 0, g_path);
  if (count == 0 && prev == 0) return;  // nothing to write, nothing to clear
  WriteFingerprint();
  char buf[64];
  sprintf_s(buf, "%d", count);
  WritePrivateProfileStringA("Cache", "PendingCount", buf, g_path);
  char key[32];
  for (int i = 0; i < count; ++i) {
    sprintf_s(key, "Pending%d", i);
    sprintf_s(buf, "%u,%u", list[i].globalSlotRva, list[i].canvasRectOffset);
    WritePrivateProfileStringA("Cache", key, buf, g_path);
  }
  for (int i = count; i < prev; ++i) {  // clear stale keys from a longer list
    sprintf_s(key, "Pending%d", i);
    WritePrivateProfileStringA("Cache", key, nullptr, g_path);
  }
  LOG("cache: stored %d PENDING candidate(s)%s", count,
      count < prev ? " (stale entries cleared)" : "");
}

bool ArmedCookieSet() {
  if (!g_path[0]) return false;
  return GetPrivateProfileIntA("Cache", "Armed", 0, g_path) != 0;
}

void WriteArmedCookie(bool set) {
  if (!g_path[0]) return;
  WritePrivateProfileStringA("Cache", "Armed", set ? "1" : nullptr, g_path);
}

}  // namespace cache

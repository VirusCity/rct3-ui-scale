#include "patch.h"

#include <windows.h>

#include <cstring>
#include <vector>

#include "MinHook.h"
#include "log.h"

namespace patch {
namespace {

struct RawPatch {
  void* addr;
  LONG original;  // first-seen original 4 bytes (idempotence)
  bool applied;
  const char* what;
};

CRITICAL_SECTION g_cs;
bool g_csInit = false;
std::vector<RawPatch> g_raw;
std::vector<void*> g_hooks;

void EnsureCs() {
  if (!g_csInit) {
    InitializeCriticalSection(&g_cs);
    g_csInit = true;
  }
}

bool AtomicWrite4(void* addr, LONG value, LONG* prev) {
  if (reinterpret_cast<uintptr_t>(addr) & 3) {
    LOG("patch: REFUSED unaligned 4-byte write at %p", addr);
    return false;
  }
  DWORD oldProt;
  if (!VirtualProtect(addr, 4, PAGE_EXECUTE_READWRITE, &oldProt)) {
    LOG("patch: VirtualProtect failed at %p (err %lu)", addr, GetLastError());
    return false;
  }
  *prev = InterlockedExchange(static_cast<volatile LONG*>(addr), value);
  DWORD ignored;
  VirtualProtect(addr, 4, oldProt, &ignored);
  return true;
}

}  // namespace

bool Init() {
  EnsureCs();
  const MH_STATUS s = MH_Initialize();
  if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
    LOG("patch: MH_Initialize failed: %s", MH_StatusToString(s));
    return false;
  }
  return true;
}

bool ApplyFloat(void* addr, float value, const char* what) {
  EnsureCs();
  LONG asLong;
  static_assert(sizeof(float) == sizeof(LONG), "x86 float/LONG size");
  memcpy(&asLong, &value, 4);

  EnterCriticalSection(&g_cs);
  RawPatch* existing = nullptr;
  for (auto& p : g_raw)
    if (p.addr == addr) existing = &p;

  LONG prev;
  if (!AtomicWrite4(addr, asLong, &prev)) {
    LeaveCriticalSection(&g_cs);
    return false;
  }

  if (existing) {
    existing->applied = true;  // keep the FIRST original — never a scaled value
  } else {
    g_raw.push_back({addr, prev, true, what});
  }
  LeaveCriticalSection(&g_cs);

  float prevF;
  memcpy(&prevF, &prev, 4);
  LOG("patch: %s @ %p: %.2f -> %.2f (reversible)", what, addr, prevF, value);
  return true;
}

// True if [addr, addr+4) is currently committed and not guarded/no-access,
// i.e. safe to write. Used to skip restoring a target whose heap object was
// already freed (e.g. the canvas root torn down at game shutdown).
bool TargetMapped(void* addr) {
  MEMORY_BASIC_INFORMATION mbi;
  if (VirtualQuery(addr, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
  if (mbi.State != MEM_COMMIT) return false;
  if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
  return true;
}

void RestoreAll() {
  EnsureCs();
  EnterCriticalSection(&g_cs);
  for (auto& p : g_raw) {
    if (!p.applied) continue;
    if (!TargetMapped(p.addr)) {
      // Object was freed (typically at shutdown) — nothing to restore. This is
      // benign: the patched value no longer exists.
      p.applied = false;
      LOG("patch: skip restore of %s @ %p — target no longer mapped (object "
          "freed)",
          p.what, p.addr);
      continue;
    }
    LONG ignored;
    if (AtomicWrite4(p.addr, p.original, &ignored)) {
      p.applied = false;
      LOG("patch: restored %s @ %p", p.what, p.addr);
    } else {
      LOG("patch: FAILED to restore %s @ %p", p.what, p.addr);
    }
  }
  LeaveCriticalSection(&g_cs);
}

void ForgetAll() {
  EnsureCs();
  EnterCriticalSection(&g_cs);
  size_t n = g_raw.size();
  g_raw.clear();
  LeaveCriticalSection(&g_cs);
  LOG("patch: forgot %zu raw-patch record(s) without restoring (reset path)",
      n);
}

size_t ActiveCount() {
  EnsureCs();
  EnterCriticalSection(&g_cs);
  size_t n = 0;
  for (const auto& p : g_raw)
    if (p.applied) ++n;
  LeaveCriticalSection(&g_cs);
  return n;
}

bool Hook(void* target, void* detour, void** original, const char* name) {
  EnsureCs();
  MH_STATUS s = MH_CreateHook(target, detour, original);
  if (s != MH_OK) {
    LOG("patch: MH_CreateHook(%s @ %p) failed: %s", name, target,
        MH_StatusToString(s));
    return false;
  }
  s = MH_EnableHook(target);  // MinHook enables atomically (thread-frozen)
  if (s != MH_OK) {
    LOG("patch: MH_EnableHook(%s @ %p) failed: %s", name, target,
        MH_StatusToString(s));
    MH_RemoveHook(target);
    return false;
  }
  EnterCriticalSection(&g_cs);
  g_hooks.push_back(target);
  LeaveCriticalSection(&g_cs);
  LOG("patch: hooked %s @ %p", name, target);
  return true;
}

void UnhookAll() {
  EnsureCs();
  EnterCriticalSection(&g_cs);
  for (void* t : g_hooks) {
    MH_DisableHook(t);
    MH_RemoveHook(t);
  }
  g_hooks.clear();
  LeaveCriticalSection(&g_cs);
}

void Shutdown() {
  RestoreAll();
  UnhookAll();
  MH_Uninitialize();
}

}  // namespace patch

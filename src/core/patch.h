// core/patch.h — reversible patch primitives.
//
// Runtime Application Safety (CLAUDE.md):
//  * Reversibility — every raw memory patch records its original bytes;
//    RestoreAll() is the DisableScaling() safety-net contract.
//  * Thread safety — value patches are 4-byte aligned InterlockedExchange
//    (atomic on x86); function hooks go through MinHook, whose enable/disable
//    is atomic with respect to executing threads.
//  * Idempotence — re-patching an already-patched address keeps the FIRST
//    recorded original, so apply/restore cycles never bake in a scaled value.
//
// All targets are runtime-discovered pointers. Nothing in this module (or its
// callers) may use fixed addresses or byte signatures — Hard Rules.
#pragma once

#include <cstddef>

namespace patch {

bool Init();      // MinHook init; call once at attach
void Shutdown();  // unhooks everything, restores raw patches

// --- Reversible raw value patches (canvas path) ------------------------
// Atomically writes a 4-byte value, recording the original for restore.
bool ApplyFloat(void* addr, float value, const char* what);

// Restores every recorded raw patch to its original bytes.
void RestoreAll();

// Drops all raw-patch records WITHOUT writing. Use on device reset, where the
// patched heap object (e.g. the canvas root's rect) is rewritten by the game
// itself for the new resolution — restoring our old value there would be
// redundant at best and could touch a reallocated object at worst.
void ForgetAll();

size_t ActiveCount();

// --- Function hooks (gate / discovery / input) --------------------------
// target MUST be a pointer obtained from a live object or export at runtime.
bool Hook(void* target, void* detour, void** original, const char* name);
void UnhookAll();

}  // namespace patch

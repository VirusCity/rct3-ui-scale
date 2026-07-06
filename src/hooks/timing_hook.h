// hooks/timing_hook.h — signature-located timing hooks on the UI creators.
//
// Signatures provide TIMING ONLY: they locate the function(s) that build the
// canvas so we can act at the one correct moment — right after the canvas
// rect is written and before the UI lays out against it. They never locate
// data; the canvas mapping always comes from passive discovery (or its cache)
// and is re-resolved fresh on every execution.
//
// Three tiers, best-available wins per entry:
//   1. INI-override signatures ([Signatures] in the ini) — porter escape hatch.
//   2. Built-in guard signatures, byte-verified against both known editions:
//      A (Complete-style): frame-install + singleton guard
//         64 A3 00 00 00 00 83 3D ?? ?? ?? ?? 00 74, prologue 55 8B EC 6A FF 68
//      B (Gold-style): guard in a frameless-SEH prologue
//         A1 ?? ?? ?? ?? 64 89 25 00 00 00 00 83 EC ?? 85 C0 74,
//         prologue 64 A1 00 00 00 00 6A FF 68
//      (The ?? bytes in the guards are the s_desktop operand — deliberately
//      wildcarded and NEVER read: timing only.)
//   3. Store-derived (data → timing): given a DISCOVERED slot RVA, find the
//      code that stores a register to that slot (A3 / 89 /r disp32 — the
//      destructor's immediate store is excluded by construction), walk back
//      to the nearest padded function prologue, hook that entry. This is the
//      tier that makes unknown editions work with zero per-version patterns.
//
// Every detour is a __fastcall(void* ecx, void* edx) passthrough so an
// unknown calling convention (cdecl/thiscall, no stack args — both verified
// editions' creators are void(void)) cannot corrupt registers. The detour:
// snapshot → original → attribute/validate via passive discovery → shrink the
// freshly-written rect (reversible patch) → state Ready. If anything fails
// validation it does NOTHING.
#pragma once

#include <cstdint>

namespace timing {

// Compile built-in + INI-override patterns. Safe to call before any hook.
bool Init();

// Tier 1+2: install guard-signature hooks (no data needed). Call at attach.
void InstallGuardTierHooks();

// Tier 3: install store-derived hooks for a discovered/cached slot RVA.
// Idempotent per resulting entry; capped. Callable at attach (cache) or
// mid-run (fresh discovery). No-op (cheap) if the slot has no in-exe store.
void InstallStoreHooksForSlot(uint32_t slotRva);

// How many in-exe code sites store to this slot. >0 ⇒ a real game-written
// global (the discriminator that separates s_desktop-class globals from
// D3D-runtime rects written only by d3d9.dll). Backed by a one-time index.
int SlotStoreSiteCount(uint32_t slotRva);

// How many in-exe code sites reference this global's address. The canvas root
// is referenced across the whole UI (hundreds); decoys by a handful — the
// discriminator that separates the ONE canvas from other device-rect globals.
// Cached per slot.
int SlotXrefCount(uint32_t slotRva);

// Number of live creator hooks (0 => scaling can never trigger; discovery
// and caching still run — spec section 6).
int InstalledCount();

}  // namespace timing

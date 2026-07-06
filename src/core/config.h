// core/config.h — user configuration, read once at attach.
//
// Scale resolution rule (CLAUDE.md "Definitions"): scaleX/scaleY come from
// either the explicit user override here, or backbuffer ÷ reference authoring
// dimension. The reference dimensions are config so users of community
// widescreen patches can correct them; they are only the DIVISOR of the auto
// rule, never something we read back from game memory.
#pragma once

struct Config {
  // --- user-facing ([Master] / [Features] / [Debug] in the shipped ini) ---
  bool enabled = true;         // [Master] Enabled — master switch
  float scaleOverride = 0.f;   // [Features] Scale — 0 = auto (bb / reference)
  // [Features] Borderless — borderless windowed at monitor resolution; also
  // lets the game render above its 1080p in-game cap (feeds the auto scale).
  bool borderless = true;

  // --- advanced (undocumented ini keys; defaults fit all known editions) --
  bool apply = true;          // false => run full discovery but never patch
  // Auto baseline: 1x at reference, proportional above it (2x at 4K with the
  // 1920x1080 default), clamped so we never shrink the UI below native.
  int referenceWidth = 1920;
  int referenceHeight = 1080;
  int borderlessWidth = 0;   // 0 = monitor native width
  int borderlessHeight = 0;  // 0 = monitor native height

  // [Gate] — startup-trap defense
  int stableFrames = 30;             // frames the backbuffer must hold steady
  int stableFramesPlaceholder = 120; // stricter when dims equal a known
                                     // device-negotiation placeholder

  // [Discovery] — passive data-flow discovery + revalidation tuning
  int discStableFrames = 60;   // N: consecutive matching ticks to trust a rect
  int discTimeoutFrames = 900; // M: revalidation window before state Failed
  int slotsPerTick = 65536;   // slot-scan budget per Present tick
  int maxRectOffset = 0x400;  // how deep into an object to look for the rect
  // Canvas discriminator: the canvas global is referenced by hundreds of code
  // sites, decoys by a handful. Accept the dominant-xref real global as the
  // canvas when its xref >= this AND is >= 2x the next candidate's.
  int minCanvasXref = 40;

  // [Signatures] — timing-hook pattern OVERRIDES (empty = built-in, which are
  // byte-verified on Complete + Gold). Timing only; never locate data.
  char sigGuardA[192] = {};
  char sigPrologueA[64] = {};
  char sigGuardB[192] = {};
  char sigPrologueB[64] = {};

  // [Debug] Logging / Verbose / Cache
  bool logEnabled = false;  // off by default; first support step: turn it on
  bool logVerbose = false;  // per-candidate seat/drop chatter (debug only)
  bool cacheEnabled = true;
};

bool LoadConfig(const char* iniPath);
const Config& GetConfig();

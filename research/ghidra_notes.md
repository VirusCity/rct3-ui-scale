# Ghidra / x64dbg notes — RCT3 (Steam, App ID 1368820)

> Build-specific addresses, offsets, signatures, and struct layouts you recover
> from **this exact executable**. Per CLAUDE.md, Claude will NEVER invent these —
> anything used in code must appear here first, captured by you.
>
> Do not paste copyrighted disassembly listings; record addresses, signatures,
> and your own descriptions. This file is gitignored.

## Module / base
- Main executable name: `____`
- Preferred image base: `____`  (note: ASLR — prefer signatures or module-relative offsets over absolute addresses)
- Build/version string if any: `____`

## Candidate: single UI scale factor (secondary strategy)
> If a global UI scale / coordinate multiplier exists, an in-memory patch may beat the render hook.
- Address / offset: `____`
- Type & current value: `____`
- Byte signature (AOB) to find it: `____`
- How you confirmed it affects the UI:

## Candidate: UI coordinate-computation routine
- Function address / signature: `____`
- What it computes (inputs → outputs):
- Where screen/resolution feeds in:

## Mouse / hit-testing (Known hard problem #2)
- Where the game reads cursor position: `____`
- Where it maps cursor → UI element: `____`
- Signature:

## Other signatures
| Purpose | Signature (AOB) | Notes |
|---------|-----------------|-------|
|         |                 |       |

#include "input_remap.h"

#include "config.h"
#include "logging.h"
#include "ui_scale.h"

namespace inputremap {
namespace {

WNDPROC g_origProc = nullptr;
HWND    g_hwnd = nullptr;

// Mouse messages whose lParam carries CLIENT-space x/y (LOWORD/HIWORD).
// Excludes WM_MOUSEWHEEL (0x20A) / WM_MOUSEHWHEEL (0x20E), whose lParam is in
// SCREEN coords, and the non-client variants.
bool IsClientMouseMsg(UINT msg) {
  return (msg >= WM_MOUSEMOVE && msg <= WM_MBUTTONDBLCLK) ||   // 0x200..0x209
         (msg >= WM_XBUTTONDOWN && msg <= WM_XBUTTONDBLCLK);   // 0x20B..0x20D
}

LRESULT CALLBACK HookProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (IsClientMouseMsg(msg) && GetConfig().remapInput) {
    const int x = static_cast<short>(LOWORD(lParam));
    const int y = static_cast<short>(HIWORD(lParam));
    int nx = 0, ny = 0;
    // Only transform when the cursor is over a currently-scaled UI element (the
    // renderer's per-frame rect list — empty when scaling is toggled off, so
    // this naturally no-ops then). Everything else (3D world) passes through
    // unchanged, keeping world picking / placement 1:1 with no jumps.
    if (uiscale::MapCursorToOriginal(x, y, nx, ny)) {
      lParam = static_cast<LPARAM>(((ny & 0xFFFF) << 16) | (nx & 0xFFFF));
    }
  }
  return CallWindowProc(g_origProc, hwnd, msg, wParam, lParam);
}

}  // namespace

void Install(HWND hwnd) {
  if (g_origProc || !hwnd) return;
  g_hwnd = hwnd;
  if (IsWindowUnicode(hwnd)) {
    g_origProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookProc)));
  } else {
    g_origProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(
        hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookProc)));
  }
  LOG("inputremap: subclassed hwnd=%p (orig proc=%p, unicode=%d)", (void*)hwnd,
      (void*)g_origProc, IsWindowUnicode(hwnd) ? 1 : 0);
}

void Remove() {
  if (!g_origProc || !g_hwnd) return;
  if (IsWindowUnicode(g_hwnd)) {
    SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(g_origProc));
  } else {
    SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(g_origProc));
  }
  g_origProc = nullptr;
  g_hwnd = nullptr;
}

}  // namespace inputremap

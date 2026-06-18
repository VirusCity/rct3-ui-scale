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
  if (IsClientMouseMsg(msg)) {
    const Config& cfg = GetConfig();
    if (cfg.remapInput && cfg.renderSideScale && cfg.scale != 1.0f) {
      RECT rc{};
      GetClientRect(hwnd, &rc);
      const float W = static_cast<float>(rc.right - rc.left);
      const float H = static_cast<float>(rc.bottom - rc.top);
      if (W > 0 && H > 0) {
        const int x = static_cast<short>(LOWORD(lParam));
        const int y = static_cast<short>(HIWORD(lParam));
        float ax, ay;
        uiscale::PickZoneAnchor(static_cast<float>(x), static_cast<float>(y),
                                0.f, 0.f, W, H, ax, ay);
        const float s = cfg.scale;
        // Inverse of the renderer's transform: screen -> original layout coord.
        const int nx = static_cast<int>(ax + (x - ax) / s + 0.5f);
        const int ny = static_cast<int>(ay + (y - ay) / s + 0.5f);
        lParam = static_cast<LPARAM>(((ny & 0xFFFF) << 16) | (nx & 0xFFFF));
      }
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

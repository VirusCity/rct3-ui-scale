#include "borderless.h"

#include <d3d9.h>

#include "config.h"
#include "logging.h"

namespace borderless {
namespace {

// Last device window we touched, so Reset / Tick can re-apply without re-deriving
// it from the device.
HWND g_wnd = nullptr;

// Window chrome we strip for a borderless window, and the bit we add.
constexpr LONG kChromeStyle = WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME |
                              WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
constexpr LONG kChromeExStyle =
    WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE;

bool Enabled() { return GetConfig().borderless; }

// Full bounds of the monitor `wnd` is on (virtual-screen coords). Falls back to
// the primary monitor's metrics if anything fails.
bool MonitorRect(HWND wnd, RECT* out) {
  HMONITOR mon = (wnd && IsWindow(wnd))
                     ? MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST)
                     : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi{sizeof(mi)};
  if (mon && GetMonitorInfo(mon, &mi)) {
    *out = mi.rcMonitor;
    return true;
  }
  out->left = 0;
  out->top = 0;
  out->right = GetSystemMetrics(SM_CXSCREEN);
  out->bottom = GetSystemMetrics(SM_CYSCREEN);
  return out->right > 0 && out->bottom > 0;
}

}  // namespace

void PrepareParams(_D3DPRESENT_PARAMETERS_* ppRaw, HWND wnd) {
  if (!Enabled() || !ppRaw) return;
  auto* pp = reinterpret_cast<D3DPRESENT_PARAMETERS*>(ppRaw);

  if (wnd && IsWindow(wnd)) g_wnd = wnd;
  RECT mon;
  if (!MonitorRect(g_wnd, &mon)) {
    LOG("borderless: no monitor rect — leaving present params unchanged.");
    return;
  }
  const UINT w = static_cast<UINT>(mon.right - mon.left);
  const UINT h = static_cast<UINT>(mon.bottom - mon.top);

  const BOOL wasWindowed = pp->Windowed;
  const UINT wasW = pp->BackBufferWidth, wasH = pp->BackBufferHeight;

  pp->Windowed = TRUE;                     // borderless == windowed, no exclusive
  pp->FullScreen_RefreshRateInHz = 0;      // must be 0 when windowed
  pp->BackBufferWidth = w;                 // render at desktop resolution (native)
  pp->BackBufferHeight = h;

  LOG("borderless: present params -> windowed %ux%u (was windowed=%d %ux%u).", w, h,
      wasWindowed ? 1 : 0, wasW, wasH);
}

void ApplyWindow(HWND wnd) {
  if (!Enabled()) return;
  if (wnd && IsWindow(wnd)) g_wnd = wnd;
  if (!g_wnd || !IsWindow(g_wnd)) return;

  RECT mon;
  if (!MonitorRect(g_wnd, &mon)) return;

  LONG style = GetWindowLong(g_wnd, GWL_STYLE);
  LONG exstyle = GetWindowLong(g_wnd, GWL_EXSTYLE);
  LONG wantStyle = (style & ~kChromeStyle) | WS_POPUP | WS_VISIBLE;
  LONG wantEx = exstyle & ~kChromeExStyle;
  if (style != wantStyle) SetWindowLong(g_wnd, GWL_STYLE, wantStyle);
  if (exstyle != wantEx) SetWindowLong(g_wnd, GWL_EXSTYLE, wantEx);

  // Cover the monitor. SWP_FRAMECHANGED so the stripped style takes effect.
  SetWindowPos(g_wnd, HWND_TOP, mon.left, mon.top, mon.right - mon.left,
               mon.bottom - mon.top,
               SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
  LOG("borderless: window covered monitor %dx%d at (%d,%d).",
      mon.right - mon.left, mon.bottom - mon.top, mon.left, mon.top);
}

void Tick() {
  if (!Enabled() || !g_wnd || !IsWindow(g_wnd)) return;
  // Re-assert only if the game put chrome back or the window no longer covers the
  // monitor (cheap checks; avoids a per-frame SetWindowPos otherwise).
  if (GetWindowLong(g_wnd, GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME)) {
    ApplyWindow(g_wnd);
    return;
  }
  RECT mon, cur;
  if (MonitorRect(g_wnd, &mon) && GetWindowRect(g_wnd, &cur) &&
      (cur.left != mon.left || cur.top != mon.top || cur.right != mon.right ||
       cur.bottom != mon.bottom)) {
    ApplyWindow(g_wnd);
  }
}

}  // namespace borderless

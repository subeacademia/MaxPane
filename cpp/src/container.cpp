#include "container.h"
#include "config.h"
#include <cstring>
#include <cstdlib>
#include <unistd.h>

// Globals (defined in main.cpp)
extern void (*g_DockWindowAddEx)(HWND, const char*, const char*, bool);
extern void (*g_DockWindowRemove)(HWND);
extern void (*g_ShowConsoleMsg)(const char*);
extern void (*g_Main_OnCommand)(int, int);
extern const char* (*g_GetExtState)(const char*, const char*);
extern void (*g_SetExtState)(const char*, const char*, const char*, bool);
extern HWND g_reaperMainHwnd;
extern int (*g_plugin_register)(const char*, void*);

#define TIMER_ID_CHECK 1
#define TIMER_INTERVAL 500
#define TIMER_ID_CAPTURE 2
#define TIMER_CAPTURE_INTERVAL 50

// Menu IDs
#define MENU_RELEASE 2000
#define MENU_CAPTURE_BY_CLICK 2001
#define MENU_KNOWN_BASE 1000
#define MENU_LAYOUT_BASE 3000
#define MENU_OPEN_WINDOWS_BASE 4000
#define MENU_OPEN_WINDOWS_MAX 4500

// Storage for open windows enumeration
struct OpenWindowEntry {
  HWND hwnd;
  char title[256];
};

static OpenWindowEntry g_openWindows[256];
static int g_openWindowCount = 0;

struct EnumOpenWindowsData {
  HWND containerHwnd;
  const WindowManager* winMgr;
};

static BOOL CALLBACK EnumOpenWindowsProc(HWND hwnd, LPARAM lParam)
{
  EnumOpenWindowsData* data = (EnumOpenWindowsData*)lParam;
  if (g_openWindowCount >= 256) return FALSE;

  if (!IsWindowVisible(hwnd)) return TRUE;

  char buf[256];
  GetWindowText(hwnd, buf, sizeof(buf));
  if (!buf[0]) return TRUE;

  // Filter out toolbars
  if (strstr(buf, "toolbar") || strstr(buf, "Toolbar")) return TRUE;

  // Filter out ReDockIt container
  if (hwnd == data->containerHwnd) return TRUE;

  // Filter out REAPER main window
  if (hwnd == g_reaperMainHwnd) return TRUE;

  // Filter out already-captured windows
  if (data->winMgr->IsWindowCaptured(hwnd)) return TRUE;

  // Filter out very short titles (likely internal)
  if (strlen(buf) < 3) return TRUE;

  g_openWindows[g_openWindowCount].hwnd = hwnd;
  strncpy(g_openWindows[g_openWindowCount].title, buf, sizeof(g_openWindows[g_openWindowCount].title) - 1);
  g_openWindows[g_openWindowCount].title[sizeof(g_openWindows[g_openWindowCount].title) - 1] = '\0';
  g_openWindowCount++;

  return TRUE;
}

ReDockItContainer::ReDockItContainer()
  : m_hwnd(nullptr)
  , m_visible(false)
{
  m_captureMode.active = false;
  m_captureMode.targetPaneId = -1;
  m_winMgr.Init();
}

ReDockItContainer::~ReDockItContainer()
{
  Shutdown();
}

bool ReDockItContainer::Create()
{
  if (m_hwnd) return true;

  m_hwnd = SWELL_CreateDialog(nullptr, nullptr, g_reaperMainHwnd, DlgProc, (LPARAM)this);

  if (!m_hwnd) return false;

  SetWindowLong(m_hwnd, GWL_USERDATA, (LONG_PTR)this);

  // Load saved state (sets preset, ratios, etc.)
  LoadState();

  // Dock into REAPER
  if (g_DockWindowAddEx) {
    g_DockWindowAddEx(m_hwnd, "ReDockIt", "ReDockIt_container", true);
  }

  SetTimer(m_hwnd, TIMER_ID_CHECK, TIMER_INTERVAL, nullptr);

  ShowWindow(m_hwnd, SW_SHOW);
  m_visible = true;

  return true;
}

void ReDockItContainer::Shutdown()
{
  if (!m_hwnd) return;

  SaveState();

  if (m_captureMode.active) {
    KillTimer(m_hwnd, TIMER_ID_CAPTURE);
    m_captureMode.active = false;
  }

  m_winMgr.ReleaseAll();
  KillTimer(m_hwnd, TIMER_ID_CHECK);

  if (g_DockWindowRemove) {
    g_DockWindowRemove(m_hwnd);
  }

  DestroyWindow(m_hwnd);
  m_hwnd = nullptr;
  m_visible = false;
}

void ReDockItContainer::Show()
{
  if (!m_hwnd) {
    Create();
    return;
  }
  ShowWindow(m_hwnd, SW_SHOW);
  m_visible = true;
}

void ReDockItContainer::Toggle()
{
  if (!m_hwnd) {
    Create();
    return;
  }
  if (m_visible) {
    ShowWindow(m_hwnd, SW_HIDE);
    m_visible = false;
  } else {
    ShowWindow(m_hwnd, SW_SHOW);
    m_visible = true;
  }
}

bool ReDockItContainer::IsVisible() const
{
  return m_visible && m_hwnd && IsWindowVisible(m_hwnd);
}

void ReDockItContainer::SetLayoutPreset(LayoutPreset preset)
{
  if (preset < 0 || preset >= PRESET_COUNT) return;

  int oldCount = m_layout.GetPaneCount();
  int newCount = PRESET_PANE_COUNT[preset];

  // Release windows in panes that will disappear
  for (int i = newCount; i < oldCount; i++) {
    m_winMgr.ReleaseWindow(i);
  }

  m_layout.SetPreset(preset);
  m_winMgr.SetActivePaneCount(newCount);

  RECT rc;
  GetClientRect(m_hwnd, &rc);
  m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
  m_winMgr.RepositionAll(m_layout);
  InvalidateRect(m_hwnd, nullptr, TRUE);
  SaveState();
}

void ReDockItContainer::CaptureDefaultWindows()
{
  if (!m_hwnd) return;

  struct { int pane; const char* name; int action; } defaults[] = {
    {0, "Media Explorer", 50124},
    {1, "FX Browser",     40271},
    {2, "Actions",        40605},
  };

  int paneCount = m_layout.GetPaneCount();
  int count = 3;
  if (count > paneCount) count = paneCount;

  for (int d = 0; d < count; d++) {
    for (int i = 0; i < NUM_KNOWN_WINDOWS; i++) {
      if (strcmp(KNOWN_WINDOWS[i].name, defaults[d].name) == 0) {
        HWND found = WindowManager::FindReaperWindow(KNOWN_WINDOWS[i].searchTitle);
        if (!found && g_Main_OnCommand) {
          g_Main_OnCommand(defaults[d].action, 0);
        }
        m_winMgr.CaptureByIndex(defaults[d].pane, i, m_hwnd);
        break;
      }
    }
  }

  RECT rc;
  GetClientRect(m_hwnd, &rc);
  m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
  m_winMgr.RepositionAll(m_layout);
}

void ReDockItContainer::CaptureWindowToPane(int paneId, const char* windowName)
{
  if (!m_hwnd) return;

  for (int i = 0; i < NUM_KNOWN_WINDOWS; i++) {
    if (strcmp(KNOWN_WINDOWS[i].name, windowName) == 0) {
      HWND found = WindowManager::FindReaperWindow(KNOWN_WINDOWS[i].searchTitle);
      if (!found && g_Main_OnCommand) {
        g_Main_OnCommand(KNOWN_WINDOWS[i].toggleActionId, 0);
      }
      m_winMgr.CaptureByIndex(paneId, i, m_hwnd);
      break;
    }
  }

  RECT rc;
  GetClientRect(m_hwnd, &rc);
  m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
  m_winMgr.RepositionAll(m_layout);
  InvalidateRect(m_hwnd, nullptr, TRUE);
}

void ReDockItContainer::SaveState()
{
  if (!g_SetExtState) return;

  char buf[256];

  // Save preset
  snprintf(buf, sizeof(buf), "%d", (int)m_layout.GetPreset());
  g_SetExtState(EXT_SECTION, "layout_preset", buf, true);

  // Save ratios
  int splitterCount = m_layout.GetSplitterCount();
  for (int i = 0; i < splitterCount; i++) {
    char key[32];
    snprintf(key, sizeof(key), "split_ratio_%d", i);
    snprintf(buf, sizeof(buf), "%.4f", m_layout.GetRatio(i));
    g_SetExtState(EXT_SECTION, key, buf, true);
  }

  // Also save legacy keys for backward compat
  snprintf(buf, sizeof(buf), "%.4f", m_layout.GetVSplitRatio());
  g_SetExtState(EXT_SECTION, "vsplit_ratio", buf, true);
  snprintf(buf, sizeof(buf), "%.4f", m_layout.GetHSplitRatio());
  g_SetExtState(EXT_SECTION, "hsplit_ratio", buf, true);

  // Save pane assignments
  int paneCount = m_layout.GetPaneCount();
  for (int i = 0; i < paneCount; i++) {
    char key[32];
    snprintf(key, sizeof(key), "pane_%d_window", i);
    const ManagedWindow* mw = m_winMgr.GetManagedWindow(i);
    if (mw && mw->captured && mw->name) {
      // For arbitrary windows, prefix with "arb:" so we know to use arbitrary capture on restore
      if (mw->isArbitrary) {
        char val[280];
        snprintf(val, sizeof(val), "arb:%s", mw->name);
        g_SetExtState(EXT_SECTION, key, val, true);
      } else {
        g_SetExtState(EXT_SECTION, key, mw->name, true);
      }
    } else {
      g_SetExtState(EXT_SECTION, key, "", true);
    }
  }
}

void ReDockItContainer::LoadState()
{
  if (!g_GetExtState) return;

  // Load preset
  const char* presetStr = g_GetExtState(EXT_SECTION, "layout_preset");
  if (presetStr && presetStr[0]) {
    int p = atoi(presetStr);
    if (p >= 0 && p < PRESET_COUNT) {
      m_layout.SetPreset((LayoutPreset)p);
      m_winMgr.SetActivePaneCount(PRESET_PANE_COUNT[p]);
    }
  }

  // Load ratios
  int splitterCount = m_layout.GetSplitterCount();
  bool hasNewRatios = false;
  for (int i = 0; i < splitterCount; i++) {
    char key[32];
    snprintf(key, sizeof(key), "split_ratio_%d", i);
    const char* val = g_GetExtState(EXT_SECTION, key);
    if (val && val[0]) {
      m_layout.SetRatio(i, (float)atof(val));
      hasNewRatios = true;
    }
  }

  // Backward compat: if no new ratio keys, read old ones
  if (!hasNewRatios) {
    const char* vs = g_GetExtState(EXT_SECTION, "vsplit_ratio");
    if (vs && vs[0]) m_layout.SetVSplitRatio((float)atof(vs));

    const char* hs = g_GetExtState(EXT_SECTION, "hsplit_ratio");
    if (hs && hs[0]) m_layout.SetHSplitRatio((float)atof(hs));
  }
}

int ReDockItContainer::PaneAtPoint(int x, int y) const
{
  int count = m_layout.GetPaneCount();
  for (int i = 0; i < count; i++) {
    const RECT& r = m_layout.GetPane(i).rect;
    if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
      return i;
  }
  return -1;
}

void ReDockItContainer::BuildOpenWindowsSubmenu(HMENU submenu, int baseId)
{
  g_openWindowCount = 0;
  EnumOpenWindowsData data = { m_hwnd, &m_winMgr };
  EnumWindows(EnumOpenWindowsProc, (LPARAM)&data);

  if (g_openWindowCount == 0) {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
    mi.fType = MFT_STRING;
    mi.fState = MFS_GRAYED;
    mi.wID = baseId;
    mi.dwTypeData = (char*)"(No windows found)";
    InsertMenuItem(submenu, 0, TRUE, &mi);
    return;
  }

  int maxItems = g_openWindowCount;
  if (maxItems > (MENU_OPEN_WINDOWS_MAX - baseId)) maxItems = MENU_OPEN_WINDOWS_MAX - baseId;
  for (int i = 0; i < maxItems; i++) {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = baseId + i;
    mi.dwTypeData = g_openWindows[i].title;
    InsertMenuItem(submenu, i, TRUE, &mi);
  }
}

// =========================================================================
// Event handlers
// =========================================================================

void ReDockItContainer::OnSize(int cx, int cy)
{
  m_layout.Recalculate(cx, cy);
  m_winMgr.RepositionAll(m_layout);
  InvalidateRect(m_hwnd, nullptr, TRUE);
}

void ReDockItContainer::OnPaint(HDC hdc)
{
  RECT rc;
  GetClientRect(m_hwnd, &rc);

  // Background
  HBRUSH bgBrush = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
  FillRect(hdc, &rc, bgBrush);
  DeleteObject(bgBrush);

  // Draw splitter bars
  HBRUSH splitterBrush = CreateSolidBrush(GetSysColor(COLOR_3DSHADOW));
  int splitterCount = m_layout.GetSplitterCount();
  for (int i = 0; i < splitterCount; i++) {
    RECT sr = m_layout.GetSplitter(i).rect;
    FillRect(hdc, &sr, splitterBrush);
  }
  DeleteObject(splitterBrush);

  // Draw pane headers and labels
  int paneCount = m_layout.GetPaneCount();
  for (int i = 0; i < paneCount; i++) {
    const Pane& pane = m_layout.GetPane(i);
    const ManagedWindow* mw = m_winMgr.GetManagedWindow(i);

    // Draw header bar
    RECT headerRect = pane.rect;
    headerRect.bottom = headerRect.top + PANE_HEADER_HEIGHT;

    HBRUSH headerBrush = CreateSolidBrush(RGB(60, 60, 60));
    FillRect(hdc, &headerRect, headerBrush);
    DeleteObject(headerBrush);

    // Header text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(200, 200, 200));
    char headerText[128];
    if (m_captureMode.active && m_captureMode.targetPaneId == i) {
      snprintf(headerText, sizeof(headerText), " Click a window to capture...");
    } else if (mw && mw->captured && mw->name) {
      snprintf(headerText, sizeof(headerText), " %s  [x]", mw->name);
    } else {
      snprintf(headerText, sizeof(headerText), " Pane %d (click to assign)", i + 1);
    }
    DrawText(hdc, headerText, -1, &headerRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Draw empty pane label if no window captured
    if (!mw || !mw->captured) {
      RECT contentRect = pane.rect;
      contentRect.top += PANE_HEADER_HEIGHT;
      SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
      DrawText(hdc, "Click header to assign a window", -1, &contentRect,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
  }
}

void ReDockItContainer::DrawPaneLabel(HDC hdc, const Pane& pane, const char* label)
{
  RECT r = pane.rect;
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
  DrawText(hdc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void ReDockItContainer::OnLButtonDown(int x, int y)
{
  int splitter = m_layout.HitTestSplitter(x, y);
  if (splitter >= 0) {
    m_layout.StartDrag(splitter);
    SetCapture(m_hwnd);
  }
}

void ReDockItContainer::OnMouseMove(int x, int y)
{
  if (m_layout.IsDragging()) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_layout.Drag(x, y, rc.right - rc.left, rc.bottom - rc.top);
    m_winMgr.RepositionAll(m_layout);
    InvalidateRect(m_hwnd, nullptr, TRUE);
  }
}

void ReDockItContainer::OnLButtonUp(int x, int y)
{
  if (m_layout.IsDragging()) {
    m_layout.EndDrag();
    ReleaseCapture();
    SaveState();
  }
}

void ReDockItContainer::OnTimer()
{
  m_winMgr.CheckAlive(m_hwnd);
}

void ReDockItContainer::OnContextMenu(int x, int y)
{
  int paneId = PaneAtPoint(x, y);
  if (paneId < 0) return;

  // If in capture mode and user clicked inside the container, cancel capture mode
  if (m_captureMode.active) {
    KillTimer(m_hwnd, TIMER_ID_CAPTURE);
    m_captureMode.active = false;
    InvalidateRect(m_hwnd, nullptr, TRUE);
    return;
  }

  HMENU menu = CreatePopupMenu();
  if (!menu) return;

  int insertPos = 0;

  // --- Layout submenu ---
  HMENU layoutMenu = CreatePopupMenu();
  if (layoutMenu) {
    for (int i = 0; i < PRESET_COUNT; i++) {
      MENUITEMINFO mi = {};
      mi.cbSize = sizeof(mi);
      mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
      mi.fType = MFT_STRING;
      mi.wID = MENU_LAYOUT_BASE + i;
      mi.dwTypeData = (char*)PRESET_NAMES[i];
      mi.fState = (i == (int)m_layout.GetPreset()) ? MFS_CHECKED : 0;
      InsertMenuItem(layoutMenu, i, TRUE, &mi);
    }

    MENUITEMINFO layoutMi = {};
    layoutMi.cbSize = sizeof(layoutMi);
    layoutMi.fMask = MIIM_SUBMENU | MIIM_TYPE;
    layoutMi.fType = MFT_STRING;
    layoutMi.hSubMenu = layoutMenu;
    layoutMi.dwTypeData = (char*)"Layout";
    InsertMenuItem(menu, insertPos++, TRUE, &layoutMi);
  }

  // --- Separator ---
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_TYPE;
    mi.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Known windows ---
  for (int i = 0; i < NUM_KNOWN_WINDOWS; i++) {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = MENU_KNOWN_BASE + i;
    mi.dwTypeData = (char*)KNOWN_WINDOWS[i].name;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Separator ---
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_TYPE;
    mi.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Open Windows submenu ---
  HMENU openWinMenu = CreatePopupMenu();
  if (openWinMenu) {
    BuildOpenWindowsSubmenu(openWinMenu, MENU_OPEN_WINDOWS_BASE);

    MENUITEMINFO owMi = {};
    owMi.cbSize = sizeof(owMi);
    owMi.fMask = MIIM_SUBMENU | MIIM_TYPE;
    owMi.fType = MFT_STRING;
    owMi.hSubMenu = openWinMenu;
    owMi.dwTypeData = (char*)"Open Windows";
    InsertMenuItem(menu, insertPos++, TRUE, &owMi);
  }

  // --- Capture by Click ---
  {
    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = MENU_CAPTURE_BY_CLICK;
    mi.dwTypeData = (char*)"Capture by Click";
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // --- Separator + Release ---
  const ManagedWindow* mw = m_winMgr.GetManagedWindow(paneId);
  if (mw && mw->captured) {
    MENUITEMINFO sep = {};
    sep.cbSize = sizeof(sep);
    sep.fMask = MIIM_TYPE;
    sep.fType = MFT_SEPARATOR;
    InsertMenuItem(menu, insertPos++, TRUE, &sep);

    MENUITEMINFO mi = {};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_ID | MIIM_TYPE;
    mi.fType = MFT_STRING;
    mi.wID = MENU_RELEASE;
    mi.dwTypeData = (char*)"Close";
    InsertMenuItem(menu, insertPos++, TRUE, &mi);
  }

  // Show menu
  POINT pt = {x, y};
  ClientToScreen(m_hwnd, &pt);

  int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
  DestroyMenu(menu);

  // --- Handle commands ---

  // Layout preset selection
  if (cmd >= MENU_LAYOUT_BASE && cmd < MENU_LAYOUT_BASE + PRESET_COUNT) {
    SetLayoutPreset((LayoutPreset)(cmd - MENU_LAYOUT_BASE));
    return;
  }

  // Known window selection
  if (cmd >= MENU_KNOWN_BASE && cmd < MENU_KNOWN_BASE + NUM_KNOWN_WINDOWS) {
    int idx = cmd - MENU_KNOWN_BASE;

    auto findWindow = [&](int widx) -> HWND {
      HWND h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[widx].searchTitle);
      if (!h && KNOWN_WINDOWS[widx].altSearchTitle) {
        h = WindowManager::FindReaperWindow(KNOWN_WINDOWS[widx].altSearchTitle);
      }
      return h;
    };

    HWND found = findWindow(idx);
    if (!found && g_Main_OnCommand) {
      g_Main_OnCommand(KNOWN_WINDOWS[idx].toggleActionId, 0);

      for (int attempt = 0; attempt < 20 && !found; attempt++) {
        #ifdef __APPLE__
        usleep(100000); // 100ms
        #endif
        found = findWindow(idx);
      }
    }

    if (found) {
      m_winMgr.CaptureByIndex(paneId, idx, m_hwnd);

      RECT rc;
      GetClientRect(m_hwnd, &rc);
      m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
      m_winMgr.RepositionAll(m_layout);
      InvalidateRect(m_hwnd, nullptr, TRUE);
      SaveState();
    }
    return;
  }

  // Open Windows selection
  if (cmd >= MENU_OPEN_WINDOWS_BASE && cmd < MENU_OPEN_WINDOWS_MAX) {
    int idx = cmd - MENU_OPEN_WINDOWS_BASE;
    if (idx >= 0 && idx < g_openWindowCount) {
      HWND targetHwnd = g_openWindows[idx].hwnd;
      const char* title = g_openWindows[idx].title;
      if (targetHwnd && IsWindow(targetHwnd)) {
        m_winMgr.CaptureArbitraryWindow(paneId, targetHwnd, title, m_hwnd);

        RECT rc;
        GetClientRect(m_hwnd, &rc);
        m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
        m_winMgr.RepositionAll(m_layout);
        InvalidateRect(m_hwnd, nullptr, TRUE);
        SaveState();
      }
    }
    return;
  }

  // Capture by Click
  if (cmd == MENU_CAPTURE_BY_CLICK) {
    m_captureMode.active = true;
    m_captureMode.targetPaneId = paneId;
    InvalidateRect(m_hwnd, nullptr, TRUE);
    SetTimer(m_hwnd, TIMER_ID_CAPTURE, TIMER_CAPTURE_INTERVAL, nullptr);
    return;
  }

  // Release
  if (cmd == MENU_RELEASE) {
    m_winMgr.ReleaseWindow(paneId);
    InvalidateRect(m_hwnd, nullptr, TRUE);
    SaveState();
    return;
  }
}

// =========================================================================
// Dialog Procedure
// =========================================================================

INT_PTR CALLBACK ReDockItContainer::DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  ReDockItContainer* self = (ReDockItContainer*)(LONG_PTR)GetWindowLong(hwnd, GWL_USERDATA);

  switch (msg) {
    case WM_INITDIALOG: {
      self = (ReDockItContainer*)lParam;
      SetWindowLong(hwnd, GWL_USERDATA, (LONG_PTR)self);
      if (self) self->m_hwnd = hwnd;
      return 0;
    }

    case WM_SIZE: {
      if (self) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int cx = rc.right - rc.left;
        int cy = rc.bottom - rc.top;
        if (cx > 0 && cy > 0) {
          self->OnSize(cx, cy);
        }
      }
      return 0;
    }

    case WM_PAINT: {
      if (self) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        self->OnPaint(hdc);
        EndPaint(hwnd, &ps);
      }
      return 0;
    }

    case WM_LBUTTONDOWN: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        // Check if click is on a pane header
        int paneCount = self->m_layout.GetPaneCount();
        for (int i = 0; i < paneCount; i++) {
          const RECT& r = self->m_layout.GetPane(i).rect;
          if (x >= r.left && x < r.right && y >= r.top && y < r.top + PANE_HEADER_HEIGHT) {
            self->OnContextMenu(x, y);
            return 0;
          }
        }
        self->OnLButtonDown(x, y);
      }
      return 0;
    }

    case WM_MOUSEMOVE: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnMouseMove(x, y);
      }
      return 0;
    }

    case WM_LBUTTONUP: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnLButtonUp(x, y);
      }
      return 0;
    }

    case WM_NCHITTEST:
      return HTCLIENT;

    case WM_SETCURSOR: {
      if (self && (HWND)wParam == hwnd) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        int splitter = self->m_layout.HitTestSplitter(pt.x, pt.y);
        if (splitter >= 0) {
          const SplitterInfo& si = self->m_layout.GetSplitter(splitter);
          if (si.orient == SPLIT_VERTICAL) {
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
          } else {
            SetCursor(LoadCursor(nullptr, IDC_SIZENS));
          }
          return 1;
        }
      }
      break;
    }

    case WM_LBUTTONDBLCLK: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnContextMenu(x, y);
      }
      return 0;
    }

    case WM_RBUTTONUP: {
      if (self) {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        self->OnContextMenu(x, y);
      }
      return 0;
    }

    case WM_RBUTTONDOWN: {
      return 0;
    }

    case WM_CONTEXTMENU: {
      if (self) {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        self->OnContextMenu(pt.x, pt.y);
      }
      return 0;
    }

    case WM_TIMER: {
      if (self && wParam == TIMER_ID_CHECK) {
        self->OnTimer();
      }
      else if (self && wParam == TIMER_ID_CAPTURE && self->m_captureMode.active) {
        // Capture mode: poll for mouse click outside our container
        POINT pt;
        GetCursorPos(&pt);
        short mouseState = GetAsyncKeyState(VK_LBUTTON);
        if (mouseState & 0x8000) {
          // Mouse button is pressed — check what's under cursor
          HWND underCursor = WindowFromPoint(pt);
          if (underCursor && underCursor != self->m_hwnd &&
              underCursor != g_reaperMainHwnd &&
              !self->m_winMgr.IsWindowCaptured(underCursor)) {
            // Find the top-level window
            HWND topLevel = underCursor;
            HWND parent = GetParent(topLevel);
            while (parent && parent != g_reaperMainHwnd) {
              topLevel = parent;
              parent = GetParent(topLevel);
            }

            if (topLevel && topLevel != self->m_hwnd && topLevel != g_reaperMainHwnd) {
              char title[256];
              GetWindowText(topLevel, title, sizeof(title));
              if (title[0]) {
                int pId = self->m_captureMode.targetPaneId;
                self->m_winMgr.CaptureArbitraryWindow(pId, topLevel, title, self->m_hwnd);

                RECT rc;
                GetClientRect(self->m_hwnd, &rc);
                self->m_layout.Recalculate(rc.right - rc.left, rc.bottom - rc.top);
                self->m_winMgr.RepositionAll(self->m_layout);
                self->SaveState();
              }
            }

            // End capture mode
            KillTimer(hwnd, TIMER_ID_CAPTURE);
            self->m_captureMode.active = false;
            InvalidateRect(self->m_hwnd, nullptr, TRUE);
          }
        }

        // ESC to cancel capture mode
        short escState = GetAsyncKeyState(VK_ESCAPE);
        if (escState & 0x8000) {
          KillTimer(hwnd, TIMER_ID_CAPTURE);
          self->m_captureMode.active = false;
          InvalidateRect(self->m_hwnd, nullptr, TRUE);
        }
      }
      return 0;
    }

    case WM_DESTROY: {
      if (self) {
        KillTimer(hwnd, TIMER_ID_CHECK);
        KillTimer(hwnd, TIMER_ID_CAPTURE);
      }
      return 0;
    }
  }

  return 0;
}

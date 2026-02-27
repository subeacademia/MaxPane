#include "window_manager.h"
#include <cstring>

WindowManager::WindowManager()
  : m_activePaneCount(3)
{
  memset(m_windows, 0, sizeof(m_windows));
  for (int i = 0; i < MAX_PANES; i++) {
    m_windows[i].paneId = i;
    m_windows[i].captured = false;
    m_windows[i].isArbitrary = false;
  }
}

void WindowManager::Init()
{
  for (int i = 0; i < MAX_PANES; i++) {
    m_windows[i].hwnd = nullptr;
    m_windows[i].originalParent = nullptr;
    m_windows[i].captured = false;
    m_windows[i].isArbitrary = false;
  }
}

void WindowManager::SetActivePaneCount(int count)
{
  if (count < 1) count = 1;
  if (count > MAX_PANES) count = MAX_PANES;
  m_activePaneCount = count;
}

struct FindWindowData {
  const char* searchTitle;
  HWND result;
};

static BOOL CALLBACK FindWindowEnumProc(HWND hwnd, LPARAM lParam)
{
  FindWindowData* data = (FindWindowData*)lParam;
  char buf[512];
  GetWindowText(hwnd, buf, sizeof(buf));
  if (!buf[0]) return TRUE;

  if (strstr(buf, "toolbar") || strstr(buf, "Toolbar")) return TRUE;

  if (strstr(buf, data->searchTitle) == buf) {
    data->result = hwnd;
    return FALSE;
  }
  return TRUE;
}

HWND WindowManager::FindReaperWindow(const char* title)
{
  if (!title) return nullptr;

  HWND hwnd = FindWindowEx(nullptr, nullptr, nullptr, title);
  if (hwnd) return hwnd;

  FindWindowData data = { title, nullptr };
  EnumWindows(FindWindowEnumProc, (LPARAM)&data);
  return data.result;
}

bool WindowManager::CaptureWindow(int paneId, const char* windowName, HWND containerHwnd)
{
  if (paneId < 0 || paneId >= m_activePaneCount) return false;

  for (int i = 0; i < NUM_KNOWN_WINDOWS; i++) {
    if (strcmp(KNOWN_WINDOWS[i].name, windowName) == 0) {
      return CaptureByIndex(paneId, i, containerHwnd);
    }
  }
  return false;
}

bool WindowManager::CaptureByIndex(int paneId, int knownWindowIndex, HWND containerHwnd)
{
  if (paneId < 0 || paneId >= m_activePaneCount) return false;
  if (knownWindowIndex < 0 || knownWindowIndex >= NUM_KNOWN_WINDOWS) return false;

  const WindowDef& def = KNOWN_WINDOWS[knownWindowIndex];

  if (m_windows[paneId].captured) {
    ReleaseWindow(paneId);
  }

  m_windows[paneId].name = def.name;
  m_windows[paneId].searchTitle = def.searchTitle;
  m_windows[paneId].toggleAction = def.toggleActionId;
  m_windows[paneId].isArbitrary = false;

  HWND hwnd = FindReaperWindow(def.searchTitle);
  if (!hwnd && def.altSearchTitle) {
    hwnd = FindReaperWindow(def.altSearchTitle);
  }
  if (!hwnd) return false;

  return DoCapture(m_windows[paneId], hwnd, containerHwnd);
}

bool WindowManager::CaptureArbitraryWindow(int paneId, HWND targetHwnd, const char* displayName, HWND containerHwnd)
{
  if (paneId < 0 || paneId >= m_activePaneCount) return false;
  if (!targetHwnd || !displayName) return false;

  if (m_windows[paneId].captured) {
    ReleaseWindow(paneId);
  }

  strncpy(m_windows[paneId].arbitraryName, displayName, sizeof(m_windows[paneId].arbitraryName) - 1);
  m_windows[paneId].arbitraryName[sizeof(m_windows[paneId].arbitraryName) - 1] = '\0';
  strncpy(m_windows[paneId].arbitrarySearchTitle, displayName, sizeof(m_windows[paneId].arbitrarySearchTitle) - 1);
  m_windows[paneId].arbitrarySearchTitle[sizeof(m_windows[paneId].arbitrarySearchTitle) - 1] = '\0';

  m_windows[paneId].name = m_windows[paneId].arbitraryName;
  m_windows[paneId].searchTitle = m_windows[paneId].arbitrarySearchTitle;
  m_windows[paneId].toggleAction = 0;
  m_windows[paneId].isArbitrary = true;

  return DoCapture(m_windows[paneId], targetHwnd, containerHwnd);
}

bool WindowManager::DoCapture(ManagedWindow& mw, HWND targetHwnd, HWND containerHwnd)
{
  if (!targetHwnd || !containerHwnd) return false;

  mw.originalParent = GetParent(targetHwnd);
  mw.originalStyle = GetWindowLong(targetHwnd, GWL_STYLE);
  mw.originalExStyle = GetWindowLong(targetHwnd, GWL_EXSTYLE);

  SetParent(targetHwnd, containerHwnd);

  LONG_PTR newStyle = WS_CHILD | WS_VISIBLE;
  SetWindowLong(targetHwnd, GWL_STYLE, newStyle);

  mw.hwnd = targetHwnd;
  mw.captured = true;

  ShowWindow(targetHwnd, SW_SHOWNA);
  InvalidateRect(targetHwnd, nullptr, TRUE);

  return true;
}

void WindowManager::ReleaseWindow(int paneId)
{
  if (paneId < 0 || paneId >= MAX_PANES) return;
  if (!m_windows[paneId].captured) return;
  DoRelease(m_windows[paneId]);
}

void WindowManager::DoRelease(ManagedWindow& mw)
{
  if (!mw.captured || !mw.hwnd) return;

  if (IsWindow(mw.hwnd)) {
    HWND restoreParent = mw.originalParent;
    if (!restoreParent || !IsWindow(restoreParent)) {
      extern HWND g_reaperMainHwnd;
      restoreParent = g_reaperMainHwnd;
    }
    SetParent(mw.hwnd, restoreParent);
    SetWindowLong(mw.hwnd, GWL_STYLE, mw.originalStyle);
    SetWindowLong(mw.hwnd, GWL_EXSTYLE, mw.originalExStyle);

    // Close the window: use toggle action for known windows, hide for arbitrary
    if (mw.toggleAction > 0 && !mw.isArbitrary) {
      extern void (*g_Main_OnCommand)(int, int);
      if (g_Main_OnCommand) {
        g_Main_OnCommand(mw.toggleAction, 0);
      }
    } else {
      ShowWindow(mw.hwnd, SW_HIDE);
    }
  }

  mw.hwnd = nullptr;
  mw.originalParent = nullptr;
  mw.captured = false;
  mw.isArbitrary = false;
}

void WindowManager::ReleaseAll()
{
  for (int i = 0; i < MAX_PANES; i++) {
    if (m_windows[i].captured) {
      DoRelease(m_windows[i]);
    }
  }
}

void WindowManager::RepositionAll(const SplitterLayout& layout)
{
  int count = layout.GetPaneCount();
  for (int i = 0; i < count; i++) {
    if (!m_windows[i].captured || !m_windows[i].hwnd) continue;
    if (!IsWindow(m_windows[i].hwnd)) continue;

    const Pane& pane = layout.GetPane(i);
    int x = pane.rect.left;
    int y = pane.rect.top + PANE_HEADER_HEIGHT;
    int w = pane.rect.right - pane.rect.left;
    int h = pane.rect.bottom - pane.rect.top - PANE_HEADER_HEIGHT;

    if (w > 0 && h > 0) {
      SetWindowPos(m_windows[i].hwnd, HWND_TOP, x, y, w, h,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
      SendMessage(m_windows[i].hwnd, WM_SIZE, 0, MAKELONG(w, h));
    }
  }
}

void WindowManager::CheckAlive(HWND containerHwnd)
{
  for (int i = 0; i < m_activePaneCount; i++) {
    if (!m_windows[i].captured) continue;
    if (!m_windows[i].hwnd || !IsWindow(m_windows[i].hwnd)) {
      m_windows[i].hwnd = nullptr;
      m_windows[i].captured = false;
      m_windows[i].isArbitrary = false;
    }
  }
}

const ManagedWindow* WindowManager::GetManagedWindow(int paneId) const
{
  if (paneId < 0 || paneId >= MAX_PANES) return nullptr;
  return &m_windows[paneId];
}

bool WindowManager::IsAnyCaptured() const
{
  for (int i = 0; i < m_activePaneCount; i++) {
    if (m_windows[i].captured) return true;
  }
  return false;
}

bool WindowManager::IsWindowCaptured(HWND hwnd) const
{
  if (!hwnd) return false;
  for (int i = 0; i < MAX_PANES; i++) {
    if (m_windows[i].captured && m_windows[i].hwnd == hwnd) return true;
  }
  return false;
}

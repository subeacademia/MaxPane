#pragma once
#include "splitter.h"
#include "window_manager.h"

// Forward declarations for REAPER API functions we import
extern void (*g_DockWindowAddEx)(HWND hwnd, const char* name, const char* identstr, bool allowShow);
extern void (*g_DockWindowRemove)(HWND);
extern void (*g_ShowConsoleMsg)(const char*);
extern void (*g_Main_OnCommand)(int, int);
extern const char* (*g_GetExtState)(const char*, const char*);
extern void (*g_SetExtState)(const char*, const char*, const char*, bool);
extern HWND g_reaperMainHwnd;
extern int (*g_plugin_register)(const char*, void*);

// Capture mode: user clicks any window to grab it into a pane
struct CaptureMode {
  bool active;
  int targetPaneId;
};

class ReDockItContainer {
public:
  ReDockItContainer();
  ~ReDockItContainer();

  bool Create();
  void Shutdown();
  void Show();
  void Toggle();
  bool IsVisible() const;

  void CaptureDefaultWindows();
  void CaptureWindowToPane(int paneId, const char* windowName);
  void SetLayoutPreset(LayoutPreset preset);

  void SaveState();
  void LoadState();

  HWND GetHwnd() const { return m_hwnd; }

  SplitterLayout& GetLayout() { return m_layout; }
  WindowManager& GetWinMgr() { return m_winMgr; }

private:
  HWND m_hwnd;
  SplitterLayout m_layout;
  WindowManager m_winMgr;
  bool m_visible;
  CaptureMode m_captureMode;

  void OnSize(int cx, int cy);
  void OnPaint(HDC hdc);
  void OnLButtonDown(int x, int y);
  void OnMouseMove(int x, int y);
  void OnLButtonUp(int x, int y);
  void OnTimer();
  void OnContextMenu(int x, int y);
  void DrawPaneLabel(HDC hdc, const Pane& pane, const char* label);

  int PaneAtPoint(int x, int y) const;
  void BuildOpenWindowsSubmenu(HMENU submenu, int baseId);

  static INT_PTR CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

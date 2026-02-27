#pragma once
#include "config.h"
#include "splitter.h"

struct ManagedWindow {
  const char* name;
  const char* searchTitle;
  int toggleAction;
  HWND hwnd;
  HWND originalParent;
  LONG_PTR originalStyle;
  LONG_PTR originalExStyle;
  int paneId;
  bool captured;
  bool isArbitrary;
  char arbitraryName[256];
  char arbitrarySearchTitle[256];
};

class WindowManager {
public:
  WindowManager();

  void Init();
  void SetActivePaneCount(int count);
  int GetActivePaneCount() const { return m_activePaneCount; }

  bool CaptureWindow(int paneId, const char* windowName, HWND containerHwnd);
  bool CaptureByIndex(int paneId, int knownWindowIndex, HWND containerHwnd);
  bool CaptureArbitraryWindow(int paneId, HWND targetHwnd, const char* displayName, HWND containerHwnd);
  void ReleaseWindow(int paneId);
  void ReleaseAll();
  void RepositionAll(const SplitterLayout& layout);
  void CheckAlive(HWND containerHwnd);

  const ManagedWindow* GetManagedWindow(int paneId) const;
  bool IsAnyCaptured() const;
  bool IsWindowCaptured(HWND hwnd) const;

  static HWND FindReaperWindow(const char* title);

private:
  ManagedWindow m_windows[MAX_PANES];
  int m_activePaneCount;
  bool DoCapture(ManagedWindow& mw, HWND targetHwnd, HWND containerHwnd);
  void DoRelease(ManagedWindow& mw);
};

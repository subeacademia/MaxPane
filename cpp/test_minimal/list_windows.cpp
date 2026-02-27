// List all visible windows - diagnostic tool
#include <cstdio>
#ifndef SWELL_PROVIDED_BY_APP
#define SWELL_PROVIDED_BY_APP
#endif
#include "swell/swell.h"
#include "reaper_plugin.h"

static HWND g_mainHwnd = nullptr;

static BOOL CALLBACK ListAllWindows(HWND hwnd, LPARAM lParam)
{
  char buf[512] = {0};
  GetWindowText(hwnd, buf, sizeof(buf));
  if (buf[0]) {
    bool visible = IsWindowVisible(hwnd);
    HWND parent = GetParent(hwnd);
    fprintf(stderr, "  HWND=%p parent=%p vis=%d title='%s'\n",
            (void*)hwnd, (void*)parent, visible, buf);
  }
  return TRUE;
}

static void ListChildWindows(HWND parent, int depth)
{
  HWND child = GetWindow(parent, GW_CHILD);
  while (child) {
    char buf[512] = {0};
    GetWindowText(child, buf, sizeof(buf));
    if (buf[0]) {
      fprintf(stderr, "  %*sChild HWND=%p vis=%d title='%s'\n",
              depth*2, "", (void*)child, IsWindowVisible(child), buf);
    }
    ListChildWindows(child, depth + 1);
    child = GetWindow(child, GW_HWNDNEXT);
  }
}

extern "C" {
REAPER_PLUGIN_DLL_EXPORT int ReaperPluginEntry(
  HINSTANCE hInstance, reaper_plugin_info_t* rec)
{
  if (!rec) return 0;
  g_mainHwnd = rec->hwnd_main;

  // Register an action that lists all windows
  int cmdId = rec->Register("command_id", (void*)"ListWindows_Debug");

  // We'll use a hookcommand to trigger the listing
  static auto hookCmd = [](int cmd, int flag) -> bool {
    if (cmd == 0) return false; // will be set below
    fprintf(stderr, "\n=== TOP-LEVEL WINDOWS ===\n");
    EnumWindows(ListAllWindows, 0);
    fprintf(stderr, "\n=== CHILDREN OF MAIN WINDOW ===\n");
    ListChildWindows(g_mainHwnd, 0);
    fprintf(stderr, "=== END ===\n");
    return true;
  };

  return 1;
}
}

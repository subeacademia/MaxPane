// Absolutely minimal REAPER extension to test loading
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#ifndef SWELL_PROVIDED_BY_APP
#define SWELL_PROVIDED_BY_APP
#endif
#include "swell/swell.h"
#endif

#include "reaper_plugin.h"

extern "C" {

REAPER_PLUGIN_DLL_EXPORT int ReaperPluginEntry(
  HINSTANCE hInstance, reaper_plugin_info_t* rec)
{
  fprintf(stderr, "[TEST_PLUGIN] ReaperPluginEntry called!\n");
  if (!rec) return 0;
  fprintf(stderr, "[TEST_PLUGIN] version=0x%x\n", rec->caller_version);

  // Get ShowConsoleMsg
  void (*showMsg)(const char*) = (void(*)(const char*))rec->GetFunc("ShowConsoleMsg");
  if (showMsg) showMsg("[TEST_PLUGIN] Loaded OK!\n");

  // Register a simple action
  int cmdId = rec->Register("command_id", (void*)"TestPlugin_Hello");
  fprintf(stderr, "[TEST_PLUGIN] cmdId=%d\n", cmdId);

  if (cmdId) {
    static gaccel_register_t accel = {{0, 0, 0}, "Test Plugin: Hello"};
    accel.accel.cmd = cmdId;
    rec->Register("gaccel", &accel);
  }

  fprintf(stderr, "[TEST_PLUGIN] Done, returning 1\n");
  return 1;
}

} // extern "C"

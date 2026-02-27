// Minimal REAPER extension with instrumented SWELL_dllMain
// This file provides its own SWELL_dllMain instead of using swell_modstub
#include <cstdio>

#ifndef SWELL_PROVIDED_BY_APP
#define SWELL_PROVIDED_BY_APP
#endif
#include "swell/swell.h"

#include "reaper_plugin.h"

// Minimal SWELL resource stubs
struct SWELL_CursorResourceIndex *SWELL_curmodule_cursorresource_head;
struct SWELL_DialogResourceIndex *SWELL_curmodule_dialogresource_head;
struct SWELL_MenuResourceIndex *SWELL_curmodule_menuresource_head;

extern "C" {

// Our own minimal SWELL_dllMain with debug output
__attribute__ ((visibility ("default")))
int SWELL_dllMain(HINSTANCE hInst, DWORD callMode, LPVOID _GetFunc)
{
  fprintf(stderr, "[TEST_PLUGIN] SWELL_dllMain called! callMode=%u, _GetFunc=%p\n",
          (unsigned)callMode, _GetFunc);

  if (callMode == DLL_PROCESS_ATTACH)
  {
    if (!_GetFunc) {
      fprintf(stderr, "[TEST_PLUGIN] SWELL_dllMain: _GetFunc is NULL, returning 0!\n");
      return 0;
    }
    fprintf(stderr, "[TEST_PLUGIN] SWELL_dllMain: DLL_PROCESS_ATTACH OK\n");
    // Skip loading SWELL functions - we don't need them for this test
  }

  fprintf(stderr, "[TEST_PLUGIN] SWELL_dllMain returning 1\n");
  return 1;
}

REAPER_PLUGIN_DLL_EXPORT int ReaperPluginEntry(
  HINSTANCE hInstance, reaper_plugin_info_t* rec)
{
  fprintf(stderr, "[TEST_PLUGIN] ReaperPluginEntry called! rec=%p\n", (void*)rec);
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

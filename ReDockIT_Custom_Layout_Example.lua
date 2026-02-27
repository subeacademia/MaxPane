-- @description ReDockIT - Custom Layout Example (Native Docking Template)
-- @author ReDockIT
-- @version 2.0.0
-- @about
--   Template for creating your own ReDockIT native docker layouts.
--   Copy this file, modify the layout, and run it as a REAPER action.
--
--   ## Layout Syntax
--
--   A layout is a table with a "zones" array. Each zone defines:
--     edge    = where to dock (0=bottom, 1=left, 2=top, 3=right)
--     size    = width (left/right) or height (top/bottom) in pixels
--     docker  = docker index (0-15) — use different indices for splits!
--     windows = list of window names (multiple = tabs in same docker)
--
--   ## Creating Splits
--
--   To split an edge into two panes, use TWO zones with the SAME edge
--   but DIFFERENT docker indices. REAPER will create a native split:
--
--     { edge = 3, size = 300, docker = 4, windows = { "FX Browser" } },
--     { edge = 3, size = 300, docker = 5, windows = { "Actions" } },
--
--   This creates a right-side panel split into top (FX Browser) and
--   bottom (Actions). Left/right edges split vertically (top/bottom).
--   Top/bottom edges split horizontally (left/right).
--
--   ## Available Window Names
--     "Mixer", "Media Explorer", "FX Browser", "Actions",
--     "Region Manager", "Routing Matrix", "Track Manager",
--     "Big Clock", "Navigator", "Undo History", "Video",
--     "Project Bay", "Virtual MIDI Keyboard", "Performance Meter"

-- ============================================================================
-- YOUR CUSTOM LAYOUT — EDIT THIS
-- ============================================================================

local MY_LAYOUT = {
  zones = {
    -- Left side: Media Explorer
    {
      edge = 1,       -- 1 = left
      size = 280,     -- width in pixels
      docker = 6,     -- docker index
      windows = { "Media Explorer" },
    },
    -- Right side top: FX Browser
    {
      edge = 3,       -- 3 = right
      size = 300,     -- width in pixels (shared with other right zones)
      docker = 4,
      windows = { "FX Browser" },
    },
    -- Right side bottom: Actions (SPLIT because same edge, different docker)
    {
      edge = 3,       -- 3 = right (same edge = creates split with docker 4)
      size = 300,
      docker = 5,
      windows = { "Actions" },
    },
    -- Bottom: Mixer
    {
      edge = 0,       -- 0 = bottom
      size = 250,     -- height in pixels
      docker = 0,
      windows = { "Mixer" },
    },
  },
}

-- ============================================================================
-- ENGINE — do not edit below
-- ============================================================================

if not reaper.JS_Window_Find then
  reaper.MB("ReDockIT requires js_ReaScriptAPI.\nInstall via ReaPack.", "ReDockIT", 0)
  return
end

local HAS_SWS = reaper.SNM_SetIntConfigVar ~= nil

local DOCK_IDENTS = {
  ["Mixer"] = "mixer", ["Media Explorer"] = "explorer",
  ["FX Browser"] = "fxbrowser", ["Actions"] = "actions",
  ["Region Manager"] = "regmgr", ["Routing Matrix"] = "routing",
  ["Track Manager"] = "trackmgr", ["Big Clock"] = "bigclock",
  ["Navigator"] = "navigator", ["Undo History"] = "undo",
  ["Project Bay"] = "projbay_0", ["Video"] = "video",
  ["Virtual MIDI Keyboard"] = "vkb", ["Performance Meter"] = "perfmon",
}

local TOGGLE_ACTIONS = {
  ["Mixer"] = 40078, ["Media Explorer"] = 50124,
  ["FX Browser"] = 40271, ["Actions"] = 40605,
  ["Region Manager"] = 40326, ["Routing Matrix"] = 40251,
  ["Track Manager"] = 40906, ["Big Clock"] = 40378,
  ["Navigator"] = 40268, ["Undo History"] = 40072,
  ["Project Bay"] = 41157, ["Video"] = 50125,
  ["Virtual MIDI Keyboard"] = 40377, ["Performance Meter"] = 40240,
}

local WINDOW_TITLES = {
  ["Mixer"] = "Mixer", ["Media Explorer"] = "Media Explorer",
  ["FX Browser"] = "Add FX", ["Actions"] = "Actions",
  ["Region Manager"] = "Region/Marker Manager", ["Routing Matrix"] = "Routing Matrix",
  ["Track Manager"] = "Track Manager", ["Big Clock"] = "Big Clock",
  ["Navigator"] = "Navigator", ["Undo History"] = "Undo History",
  ["Project Bay"] = "Project Bay", ["Video"] = "Video Window",
  ["Virtual MIDI Keyboard"] = "Virtual MIDI Keyboard",
  ["Performance Meter"] = "Performance Meter",
}

local function is_open(name)
  local title = WINDOW_TITLES[name]
  if not title then return false end
  return reaper.JS_Window_Find(title, false) ~= nil
end

local function close_win(name)
  if TOGGLE_ACTIONS[name] and is_open(name) then
    reaper.Main_OnCommand(TOGGLE_ACTIONS[name], 0)
  end
end

local function open_win(name)
  if TOGGLE_ACTIONS[name] and not is_open(name) then
    reaper.Main_OnCommand(TOGGLE_ACTIONS[name], 0)
  end
end

-- Collect all windows
local all_windows = {}
for _, zone in ipairs(MY_LAYOUT.zones) do
  for _, w in ipairs(zone.windows) do all_windows[w] = true end
end

-- Close all
for name, _ in pairs(all_windows) do close_win(name) end

-- Configure dockers
for _, zone in ipairs(MY_LAYOUT.zones) do
  if HAS_SWS then
    reaper.SNM_SetIntConfigVar("dockermode" .. zone.docker, zone.edge)
    local size_var = ({[0]="dockheight", [1]="dockheight_l", [2]="dockheight_t", [3]="dockheight_r"})[zone.edge]
    if size_var then reaper.SNM_SetIntConfigVar(size_var, zone.size) end
  end
  for _, wname in ipairs(zone.windows) do
    local ident = DOCK_IDENTS[wname]
    if ident then reaper.Dock_UpdateDockID(ident, zone.docker) end
  end
end

-- Reopen with deferred execution
local open_list = {}
for name, _ in pairs(all_windows) do table.insert(open_list, name) end
local open_idx = 1

local function deferred_open()
  if open_idx > #open_list then
    reaper.DockWindowRefresh()
    reaper.ShowConsoleMsg("[ReDockIT] Custom layout applied (native docking).\n")
    return
  end
  open_win(open_list[open_idx])
  open_idx = open_idx + 1
  reaper.defer(deferred_open)
end

reaper.defer(deferred_open)

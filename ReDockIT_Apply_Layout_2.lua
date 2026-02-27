-- @description ReDockIT - Apply Saved Layout 2 (Native Docking)
-- @author ReDockIT
-- @version 2.0.0
-- @about Quick-apply script for ReDockIT saved layout slot 2. Bind to a shortcut.

if not reaper.JS_Window_Find then
  reaper.MB("ReDockIT requires js_ReaScriptAPI.\nInstall via ReaPack.", "ReDockIT", 0)
  return
end

local EXT_SECTION = "ReDockIT"
local HAS_SWS = reaper.SNM_SetIntConfigVar ~= nil
local SLOT = 2

local function deserialize(str)
  if not str or str == "" then return nil end
  local func = load("return " .. str)
  if not func then return nil end
  setfenv(func, {})
  local ok, result = pcall(func)
  return ok and result or nil
end

local DOCK_IDENTS = {
  ["Mixer"] = "mixer", ["Media Explorer"] = "explorer",
  ["FX Browser"] = "fxbrowser", ["Actions"] = "actions",
  ["Region Manager"] = "regmgr", ["Routing Matrix"] = "routing",
  ["Track Manager"] = "trackmgr", ["Big Clock"] = "bigclock",
  ["Navigator"] = "navigator", ["Undo History"] = "undo",
  ["Project Bay"] = "projbay_0", ["Video"] = "video",
}

local TOGGLE_ACTIONS = {
  ["Mixer"] = 40078, ["Media Explorer"] = 50124,
  ["FX Browser"] = 40271, ["Actions"] = 40605,
  ["Region Manager"] = 40326, ["Routing Matrix"] = 40251,
  ["Track Manager"] = 40906, ["Big Clock"] = 40378,
  ["Navigator"] = 40268, ["Undo History"] = 40072,
  ["Project Bay"] = 41157, ["Video"] = 50125,
}

local WINDOW_TITLES = {
  ["Mixer"] = "Mixer", ["Media Explorer"] = "Media Explorer",
  ["FX Browser"] = "Add FX", ["Actions"] = "Actions",
  ["Region Manager"] = "Region/Marker Manager", ["Routing Matrix"] = "Routing Matrix",
  ["Track Manager"] = "Track Manager", ["Big Clock"] = "Big Clock",
  ["Navigator"] = "Navigator", ["Undo History"] = "Undo History",
  ["Project Bay"] = "Project Bay", ["Video"] = "Video Window",
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

local data = deserialize(reaper.GetExtState(EXT_SECTION, "layout_" .. SLOT))
if not data or not data.layout then
  reaper.MB("No layout saved in slot " .. SLOT .. ".\nUse ReDockIT Manager to save a layout.",
    "ReDockIT", 0)
  return
end

local layout = data.layout
local all_windows = {}
for _, zone in ipairs(layout.zones) do
  for _, w in ipairs(zone.windows) do all_windows[w] = true end
end

for name, _ in pairs(all_windows) do close_win(name) end
if layout.hide then
  for _, name in ipairs(layout.hide) do close_win(name) end
end

for _, zone in ipairs(layout.zones) do
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

local open_list = {}
for name, _ in pairs(all_windows) do table.insert(open_list, name) end
local open_idx = 1

local function deferred_open()
  if open_idx > #open_list then
    reaper.DockWindowRefresh()
    reaper.ShowConsoleMsg("[ReDockIT] Layout " .. SLOT .. " applied.\n")
    return
  end
  open_win(open_list[open_idx])
  open_idx = open_idx + 1
  reaper.defer(deferred_open)
end

reaper.defer(deferred_open)

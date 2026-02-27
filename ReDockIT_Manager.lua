-- @description ReDockIT v3 - ImGui Tiling Container for REAPER
-- @author ReDockIT
-- @version 3.0.1
-- @provides [main] ReDockIT_Manager.lua
-- @about
--   # ReDockIT v3 - ImGui Tiling Container
--   Dock this window in REAPER's right docker, then drag splitters to resize.
--   REAPER windows (Media Explorer, FX Browser, Actions, etc.) are positioned
--   as borderless overlays precisely matching each zone.
--
--   ## Requirements
--   - REAPER v7.0+, ReaImGui, js_ReaScriptAPI

-- =============================================================================
-- DEPENDENCY CHECK
-- =============================================================================

if not reaper.ImGui_CreateContext then
  reaper.MB(
    "ReDockIT v3 requires ReaImGui.\n\n" ..
    "Install via ReaPack > Browse packages > 'ReaImGui'",
    "ReDockIT", 0)
  return
end

if not reaper.JS_Window_Find then
  reaper.MB(
    "ReDockIT v3 requires js_ReaScriptAPI.\n\n" ..
    "Install via ReaPack > Browse packages > 'js_ReaScriptAPI'",
    "ReDockIT", 0)
  return
end

-- =============================================================================
-- CONSTANTS
-- =============================================================================

local SCRIPT_NAME = "ReDockIT"
local EXT_SECTION = "ReDockIT_v3"
local SPLITTER_PX = 6
local MIN_PANE_PX = 60

local KNOWN_WINDOWS = {
  ["Media Explorer"]  = { title = "Media Explorer",          action = 50124 },
  ["FX Browser"]      = { title = "Add FX",                  action = 40271 },
  ["Actions"]         = { title = "Actions",                 action = 40605 },
  ["Mixer"]           = { title = "Mixer",                   action = 40078 },
  ["Region Manager"]  = { title = "Region/Marker Manager",   action = 40326 },
  ["Routing Matrix"]  = { title = "Routing Matrix",          action = 40251 },
  ["Track Manager"]   = { title = "Track Manager",           action = 40906 },
  ["Big Clock"]       = { title = "Big Clock",               action = 40378 },
  ["Navigator"]       = { title = "Navigator",               action = 40268 },
  ["Undo History"]    = { title = "Undo History",            action = 40072 },
  ["Project Bay"]     = { title = "Project Bay",             action = 41157 },
}

-- =============================================================================
-- ImGui COLOR HELPER
-- =============================================================================
-- Dear ImGui uses 0xAABBGGRR format (Alpha high byte, then BGR)

local function rgba(r, g, b, a)
  local ri = math.floor(r * 255 + 0.5)
  local gi = math.floor(g * 255 + 0.5)
  local bi = math.floor(b * 255 + 0.5)
  local ai = math.floor((a or 1.0) * 255 + 0.5)
  return (ai << 24) | (bi << 16) | (gi << 8) | ri
end

local COL = {
  bg             = rgba(0.14, 0.14, 0.17, 1.0),
  splitter       = rgba(0.30, 0.30, 0.35, 1.0),
  splitter_hover = rgba(0.45, 0.60, 0.90, 1.0),
  splitter_drag  = rgba(0.55, 0.70, 1.00, 1.0),
  pane_bg        = rgba(0.11, 0.11, 0.14, 1.0),
  pane_label     = rgba(0.55, 0.55, 0.60, 1.0),
  dot            = rgba(0.60, 0.60, 0.65, 0.8),
}

-- =============================================================================
-- LAYOUT TREE
-- =============================================================================
-- { type="hsplit", ratio=0.6, children={node1, node2} }  -- left|right
-- { type="vsplit", ratio=0.5, children={node1, node2} }  -- top/bottom
-- { type="window", name="Media Explorer" }                -- leaf

local DEFAULT_LAYOUT = {
  type = "hsplit",
  ratio = 0.55,
  children = {
    { type = "window", name = "Media Explorer" },
    {
      type = "vsplit",
      ratio = 0.50,
      children = {
        { type = "window", name = "FX Browser" },
        { type = "window", name = "Actions" },
      },
    },
  },
}

-- =============================================================================
-- LAYOUT RESOLVER
-- =============================================================================

local function resolve_layout(node, rect, windows, splitters, prefix)
  windows   = windows   or {}
  splitters = splitters or {}
  prefix    = prefix    or "s"

  if not node or not rect then return windows, splitters end
  if rect.w < 10 or rect.h < 10 then return windows, splitters end

  if node.type == "window" then
    table.insert(windows, {
      name = node.name,
      x = math.floor(rect.x),
      y = math.floor(rect.y),
      w = math.floor(rect.w),
      h = math.floor(rect.h),
    })

  elseif node.type == "hsplit" and node.children and #node.children == 2 then
    local half = math.floor(SPLITTER_PX / 2)
    local split_x = rect.x + math.floor(rect.w * node.ratio)

    resolve_layout(node.children[1],
      { x = rect.x, y = rect.y, w = split_x - rect.x - half, h = rect.h },
      windows, splitters, prefix .. "L")

    table.insert(splitters, {
      id = prefix, orientation = "h", node = node, parent_rect = rect,
      x = split_x - half, y = rect.y, w = SPLITTER_PX, h = rect.h,
    })

    local right_x = split_x + half
    resolve_layout(node.children[2],
      { x = right_x, y = rect.y, w = rect.x + rect.w - right_x, h = rect.h },
      windows, splitters, prefix .. "R")

  elseif node.type == "vsplit" and node.children and #node.children == 2 then
    local half = math.floor(SPLITTER_PX / 2)
    local split_y = rect.y + math.floor(rect.h * node.ratio)

    resolve_layout(node.children[1],
      { x = rect.x, y = rect.y, w = rect.w, h = split_y - rect.y - half },
      windows, splitters, prefix .. "T")

    table.insert(splitters, {
      id = prefix, orientation = "v", node = node, parent_rect = rect,
      x = rect.x, y = split_y - half, w = rect.w, h = SPLITTER_PX,
    })

    local bot_y = split_y + half
    resolve_layout(node.children[2],
      { x = rect.x, y = bot_y, w = rect.w, h = rect.y + rect.h - bot_y },
      windows, splitters, prefix .. "B")
  end

  return windows, splitters
end

-- =============================================================================
-- WINDOW CONTROLLER
-- =============================================================================

local managed_windows = {}   -- { [name] = { hwnd, orig_style } }
local last_rects = {}        -- { [name] = {x,y,w,h} } for skip-if-unchanged
local init_done = {}         -- { [name] = true } windows already set up

local function find_window(name)
  local info = KNOWN_WINDOWS[name]
  if not info then return nil end

  local hwnd = reaper.JS_Window_Find(info.title, false)
  if hwnd then return hwnd end

  -- Search all top-level windows
  local arr = reaper.new_array({}, 1024)
  reaper.JS_Window_ArrayAllTop(arr)
  for _, addr in ipairs(arr.table()) do
    local wnd = reaper.JS_Window_HandleFromAddress(addr)
    if wnd then
      local t = reaper.JS_Window_GetTitle(wnd)
      if t and t:find(info.title, 1, true) then return wnd end
    end
  end
  return nil
end

-- Validate that a cached HWND is still alive and usable.
local function hwnd_valid(hwnd)
  if not hwnd then return false end
  local title = reaper.JS_Window_GetTitle(hwnd)
  return title ~= nil and title ~= ""
end

local function place_window(name, x, y, w, h)
  -- Skip if nothing changed
  local lr = last_rects[name]
  if lr and lr.x == x and lr.y == y and lr.w == w and lr.h == h then
    return
  end

  x = math.floor(x)
  y = math.floor(y)
  w = math.max(math.floor(w), MIN_PANE_PX)
  h = math.max(math.floor(h), MIN_PANE_PX)

  -- If we already have a cached, valid HWND — just reposition it
  local cached = managed_windows[name]
  if cached and hwnd_valid(cached.hwnd) then
    reaper.JS_Window_SetPosition(cached.hwnd, x, y, w, h, "", "")
    last_rects[name] = { x = x, y = y, w = w, h = h }
    return
  end

  -- First-time setup: find or open the window (only once, not every frame)
  if init_done[name] then
    -- We already tried to init this window but HWND became invalid.
    -- Don't spam toggle actions. Try a simple find only.
    local hwnd = find_window(name)
    if not hwnd then return end
    managed_windows[name] = { hwnd = hwnd, orig_style = nil }
    reaper.JS_Window_SetPosition(hwnd, x, y, w, h, "", "")
    last_rects[name] = { x = x, y = y, w = w, h = h }
    return
  end

  -- === ONE-TIME INITIALIZATION ===
  init_done[name] = true

  -- Find existing window
  local hwnd = find_window(name)

  -- If not found, open it via toggle action (once!)
  if not hwnd then
    local info = KNOWN_WINDOWS[name]
    if info and info.action then
      reaper.Main_OnCommand(info.action, 0)
      hwnd = find_window(name)
    end
  end

  if not hwnd then return end

  -- Undock if it's docked natively
  local dock_idx = reaper.DockIsChildOfDock(hwnd)
  if dock_idx >= 0 then
    reaper.DockWindowRemove(hwnd)
    hwnd = find_window(name)
    if not hwnd then return end
  end

  -- Remove window borders for seamless look
  local orig_style = reaper.JS_Window_GetLong(hwnd, "STYLE")
  local new_style = orig_style & ~0x00C00000 & ~0x00040000
  reaper.JS_Window_SetLong(hwnd, "STYLE", new_style)

  -- Cache it
  managed_windows[name] = { hwnd = hwnd, orig_style = orig_style }

  -- Position
  reaper.JS_Window_SetPosition(hwnd, x, y, w, h, "", "")
  reaper.JS_Window_Show(hwnd, "SHOWNA")

  last_rects[name] = { x = x, y = y, w = w, h = h }
end

local function cleanup_windows()
  for name, info in pairs(managed_windows) do
    if info.hwnd and info.orig_style then
      reaper.JS_Window_SetLong(info.hwnd, "STYLE", info.orig_style)
      reaper.JS_Window_Show(info.hwnd, "SHOW")
    end
  end
  managed_windows = {}
  last_rects = {}
end

-- =============================================================================
-- PERSISTENCE (Lua 5.4 compatible)
-- =============================================================================

local function serialize(tbl, indent)
  indent = indent or 0
  local pad = string.rep("  ", indent)
  local pad1 = string.rep("  ", indent + 1)
  if type(tbl) ~= "table" then
    if type(tbl) == "string" then return string.format("%q", tbl) end
    return tostring(tbl)
  end
  local parts = {}
  for i, v in ipairs(tbl) do
    parts[i] = pad1 .. serialize(v, indent + 1)
  end
  for k, v in pairs(tbl) do
    if type(k) == "string" then
      table.insert(parts, pad1 .. k .. " = " .. serialize(v, indent + 1))
    end
  end
  return "{\n" .. table.concat(parts, ",\n") .. "\n" .. pad .. "}"
end

local function deserialize(str)
  if not str or str == "" then return nil end
  local func = load("return " .. str, "data", "t", {})
  if not func then return nil end
  local ok, result = pcall(func)
  return ok and result or nil
end

local function save_layout(layout)
  reaper.SetExtState(EXT_SECTION, "layout", serialize(layout), true)
end

local function load_layout()
  return deserialize(reaper.GetExtState(EXT_SECTION, "layout"))
end

-- =============================================================================
-- SPLITTER STATE
-- =============================================================================

local drag = {
  active = false,
  node = nil,
  orient = nil,
  parent = nil,
}

-- =============================================================================
-- MAIN
-- =============================================================================

local ctx = reaper.ImGui_CreateContext(SCRIPT_NAME .. " v3")
local layout = load_layout() or DEFAULT_LAYOUT
local frame_count = 0

reaper.atexit(function()
  cleanup_windows()
  save_layout(layout)
end)

local function loop()
  -- Set initial size on first frame
  if frame_count == 0 then
    reaper.ImGui_SetNextWindowSize(ctx, 500, 600, reaper.ImGui_Cond_FirstUseEver())
  end
  frame_count = frame_count + 1

  -- Style
  reaper.ImGui_PushStyleColor(ctx, reaper.ImGui_Col_WindowBg(), COL.bg)
  reaper.ImGui_PushStyleVar(ctx, reaper.ImGui_StyleVar_WindowPadding(), 0, 0)

  local flags = reaper.ImGui_WindowFlags_NoScrollbar()
              | reaper.ImGui_WindowFlags_NoScrollWithMouse()

  local visible, open = reaper.ImGui_Begin(ctx, SCRIPT_NAME .. " v3###redockit_main", true, flags)

  reaper.ImGui_PopStyleVar(ctx)
  reaper.ImGui_PopStyleColor(ctx)

  if visible then
    -- Get content area in screen coordinates
    local cx, cy = reaper.ImGui_GetCursorScreenPos(ctx)
    local cw, ch = reaper.ImGui_GetContentRegionAvail(ctx)

    -- Guard: only position windows if container has real size
    if cw > 50 and ch > 50 then

      -- Resolve layout tree → flat window rects + splitter rects
      local win_rects, sp_rects = resolve_layout(layout,
        { x = cx, y = cy, w = cw, h = ch })

      -- Mouse state
      local mx, my = reaper.ImGui_GetMousePos(ctx)
      local m_down = reaper.ImGui_IsMouseDown(ctx, 0)

      -- Get the foreground draw list (draws on top of everything in the window)
      local dl = reaper.ImGui_GetForegroundDrawList(ctx)

      -- Draw pane backgrounds (label showing which window goes where)
      for _, wr in ipairs(win_rects) do
        reaper.ImGui_DrawList_AddRectFilled(dl,
          wr.x, wr.y, wr.x + wr.w, wr.y + wr.h, COL.pane_bg)
        reaper.ImGui_DrawList_AddText(dl,
          wr.x + 6, wr.y + 3, COL.pane_label, wr.name)
      end

      -- Detect hovered splitter
      local hovered_sp = nil
      if not drag.active then
        for _, sp in ipairs(sp_rects) do
          if mx >= sp.x and mx < sp.x + sp.w
          and my >= sp.y and my < sp.y + sp.h then
            hovered_sp = sp
            break
          end
        end
      end

      -- Draw splitters
      for _, sp in ipairs(sp_rects) do
        local col = COL.splitter
        if drag.active and drag.node == sp.node then
          col = COL.splitter_drag
        elseif hovered_sp and hovered_sp.id == sp.id then
          col = COL.splitter_hover
        end

        reaper.ImGui_DrawList_AddRectFilled(dl,
          sp.x, sp.y, sp.x + sp.w, sp.y + sp.h, col)

        -- Grip dots
        local cx2 = sp.x + sp.w / 2
        local cy2 = sp.y + sp.h / 2
        if sp.orientation == "h" then
          for dy = -8, 8, 8 do
            reaper.ImGui_DrawList_AddCircleFilled(dl, cx2, cy2 + dy, 1.5, COL.dot)
          end
        else
          for dx = -8, 8, 8 do
            reaper.ImGui_DrawList_AddCircleFilled(dl, cx2 + dx, cy2, 1.5, COL.dot)
          end
        end
      end

      -- Set cursor for hovered/dragging splitter
      if hovered_sp or drag.active then
        local ori = drag.active and drag.orient or (hovered_sp and hovered_sp.orientation)
        if ori == "h" then
          reaper.ImGui_SetMouseCursor(ctx, reaper.ImGui_MouseCursor_ResizeEW())
        else
          reaper.ImGui_SetMouseCursor(ctx, reaper.ImGui_MouseCursor_ResizeNS())
        end
      end

      -- Start drag
      if m_down and not drag.active and hovered_sp then
        drag.active = true
        drag.node   = hovered_sp.node
        drag.orient = hovered_sp.orientation
        drag.parent = hovered_sp.parent_rect
      end

      -- Continue drag
      if drag.active and m_down and drag.node and drag.parent then
        local pr = drag.parent
        local new_ratio
        if drag.orient == "h" then
          new_ratio = (mx - pr.x) / pr.w
        else
          new_ratio = (my - pr.y) / pr.h
        end
        drag.node.ratio = math.max(0.1, math.min(0.9, new_ratio))
        last_rects = {} -- force reposition
      end

      -- End drag
      if not m_down and drag.active then
        drag.active = false
        drag.node   = nil
        drag.orient = nil
        drag.parent = nil
        save_layout(layout)
      end

      -- Position REAPER windows on top of the zones
      for _, wr in ipairs(win_rects) do
        if KNOWN_WINDOWS[wr.name] then
          place_window(wr.name, wr.x, wr.y, wr.w, wr.h)
        end
      end

    else
      -- Container too small — show help text
      local dl = reaper.ImGui_GetForegroundDrawList(ctx)
      reaper.ImGui_DrawList_AddText(dl, cx + 10, cy + 10, COL.pane_label,
        "Dock this window in REAPER's docker (drag to right edge)")
    end

    reaper.ImGui_End(ctx)
  end

  if open then
    reaper.defer(loop)
  else
    cleanup_windows()
    save_layout(layout)
  end
end

reaper.defer(loop)

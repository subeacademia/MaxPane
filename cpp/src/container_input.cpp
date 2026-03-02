// container_input.cpp — Input handling for ReDockItContainer
// (mouse events, tab drag/drop, hit testing, resize)
#include "container.h"
#include "config.h"

// =========================================================================
// Helpers
// =========================================================================

int ReDockItContainer::PaneAtPoint(int x, int y) const
{
  int nodeIdx = m_tree.LeafAtPoint(x, y);
  if (nodeIdx < 0) return -1;
  return m_tree.GetPaneId(nodeIdx);
}

// =========================================================================
// Tab bar layout calculation (shared by draw, hit-test, close-button)
// =========================================================================

ReDockItContainer::TabBarLayout ReDockItContainer::CalcTabBarLayout(int paneId) const
{
  TabBarLayout lay = {};
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || ps->tabCount == 0) return lay;

  const RECT& r = m_tree.GetPaneRect(paneId);
  int paneWidth = r.right - r.left;
  int totalWidth = paneWidth - PANE_MENU_BTN_WIDTH;
  if (totalWidth < TAB_MIN_WIDTH) totalWidth = TAB_MIN_WIDTH;

  int capacity = totalWidth / TAB_MIN_WIDTH;
  if (capacity < 1) capacity = 1;

  if (ps->tabCount <= capacity) {
    // No overflow
    lay.hasOverflow = false;
    lay.hasLeftArrow = false;
    lay.hasRightArrow = false;
    lay.firstVisible = 0;
    lay.visibleCount = ps->tabCount;
    lay.tabAreaLeft = r.left;
    lay.tabAreaRight = r.left + totalWidth;
    lay.tabWidth = totalWidth / ps->tabCount;
    if (lay.tabWidth > TAB_MAX_WIDTH) lay.tabWidth = TAB_MAX_WIDTH;
  } else {
    // Overflow: reserve space for arrows
    lay.hasOverflow = true;
    int arrowSpace = 2 * TAB_SCROLL_ARROW_WIDTH;
    int tabAreaWidth = totalWidth - arrowSpace;
    if (tabAreaWidth < TAB_MIN_WIDTH) tabAreaWidth = TAB_MIN_WIDTH;

    lay.visibleCount = tabAreaWidth / TAB_MIN_WIDTH;
    if (lay.visibleCount < 1) lay.visibleCount = 1;
    if (lay.visibleCount > ps->tabCount) lay.visibleCount = ps->tabCount;

    // Clamp scroll offset
    int maxOffset = ps->tabCount - lay.visibleCount;
    if (maxOffset < 0) maxOffset = 0;
    int offset = m_tabScrollOffset[paneId];
    if (offset < 0) offset = 0;
    if (offset > maxOffset) offset = maxOffset;

    lay.firstVisible = offset;
    lay.hasLeftArrow = (offset > 0);
    lay.hasRightArrow = (offset + lay.visibleCount < ps->tabCount);

    lay.tabAreaLeft = r.left + TAB_SCROLL_ARROW_WIDTH;
    lay.tabAreaRight = lay.tabAreaLeft + tabAreaWidth;
    lay.tabWidth = tabAreaWidth / lay.visibleCount;
    if (lay.tabWidth < TAB_MIN_WIDTH) lay.tabWidth = TAB_MIN_WIDTH;
    if (lay.tabWidth > TAB_MAX_WIDTH) lay.tabWidth = TAB_MAX_WIDTH;
  }

  return lay;
}

RECT ReDockItContainer::GetTabRect(int paneId, int tabIdx) const
{
  const RECT& r = m_tree.GetPaneRect(paneId);
  int tabBarTop = r.top;
  int tabBarBottom = tabBarTop + TAB_BAR_HEIGHT;
  TabBarLayout lay = CalcTabBarLayout(paneId);

  // If tab is not in visible range, return empty rect
  if (tabIdx < lay.firstVisible || tabIdx >= lay.firstVisible + lay.visibleCount) {
    RECT empty = {0, 0, 0, 0};
    return empty;
  }

  int displayIdx = tabIdx - lay.firstVisible;
  int tabLeft = lay.tabAreaLeft + displayIdx * lay.tabWidth;
  int tabRight = tabLeft + lay.tabWidth;
  if (tabRight > lay.tabAreaRight) tabRight = lay.tabAreaRight;

  RECT tr = { tabLeft, tabBarTop, tabRight, tabBarBottom };
  return tr;
}

// =========================================================================
// Tab hit testing
// =========================================================================

// Returns: >=0 tab index, -1 miss, -2 menu button, -3 left arrow, -4 right arrow
int ReDockItContainer::TabHitTest(int paneId, int x, int y) const
{
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || ps->tabCount == 0) return -1;

  const RECT& paneRect = m_tree.GetPaneRect(paneId);
  int tabBarTop = paneRect.top;
  int tabBarBottom = tabBarTop + TAB_BAR_HEIGHT;

  if (y < tabBarTop || y >= tabBarBottom) return -1;

  // Menu button (rightmost PANE_MENU_BTN_WIDTH pixels)
  if (x >= paneRect.right - PANE_MENU_BTN_WIDTH) return -2;

  TabBarLayout lay = CalcTabBarLayout(paneId);

  // Left scroll arrow
  if (lay.hasLeftArrow && x >= paneRect.left && x < lay.tabAreaLeft) return -3;

  // Right scroll arrow
  if (lay.hasRightArrow && x >= lay.tabAreaRight && x < paneRect.right - PANE_MENU_BTN_WIDTH) return -4;

  // Tab area
  if (x < lay.tabAreaLeft || x >= lay.tabAreaRight) return -1;
  int relX = x - lay.tabAreaLeft;
  int displayIdx = relX / lay.tabWidth;
  if (displayIdx < 0 || displayIdx >= lay.visibleCount) return -1;
  int tabIdx = lay.firstVisible + displayIdx;
  if (tabIdx >= ps->tabCount) return -1;
  return tabIdx;
}

bool ReDockItContainer::IsOnTabCloseButton(int paneId, int tabIndex, int x, int y) const
{
  const PaneState* ps = m_winMgr.GetPaneState(paneId);
  if (!ps || tabIndex < 0 || tabIndex >= ps->tabCount) return false;

  RECT tr = GetTabRect(paneId, tabIndex);
  // Empty rect means tab not visible
  if (tr.left == 0 && tr.right == 0) return false;

  int tabBarTop = m_tree.GetPaneRect(paneId).top;
  int closeRight = tr.right - CLOSE_BTN_RIGHT_MARGIN;
  int closeLeft = closeRight - CLOSE_BTN_WIDTH;
  int closeTop = tabBarTop + CLOSE_BTN_VERT_MARGIN;
  int closeBottom = tabBarTop + TAB_BAR_HEIGHT - CLOSE_BTN_VERT_MARGIN;

  return (x >= closeLeft && x <= closeRight && y >= closeTop && y <= closeBottom);
}

// =========================================================================
// Drag and drop
// =========================================================================

void ReDockItContainer::StartTabDrag(int paneId, int tabIndex, int x, int y)
{
  m_dragState.active = false;
  m_dragState.sourcePaneId = paneId;
  m_dragState.sourceTabIndex = tabIndex;
  m_dragState.startPt.x = x;
  m_dragState.startPt.y = y;
  m_dragState.highlightPaneId = -1;
  m_dragState.dragStarted = false;
  SetCapture(m_hwnd);
}

void ReDockItContainer::UpdateTabDrag(int x, int y)
{
  if (!m_dragState.dragStarted) {
    int dx = x - m_dragState.startPt.x;
    int dy = y - m_dragState.startPt.y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx < DRAG_THRESHOLD_PX && ady < DRAG_THRESHOLD_PX) return;
    m_dragState.dragStarted = true;
    m_dragState.active = true;
  }

  int oldHighlight = m_dragState.highlightPaneId;
  m_dragState.highlightPaneId = PaneAtPoint(x, y);

  if (m_dragState.highlightPaneId == m_dragState.sourcePaneId) {
    m_dragState.highlightPaneId = -1;
  }

  if (m_dragState.highlightPaneId >= 0) {
    if (m_winMgr.GetTabCount(m_dragState.highlightPaneId) >= MAX_TABS_PER_PANE) {
      m_dragState.highlightPaneId = -1;
    }
  }

  if (m_dragState.highlightPaneId != oldHighlight) {
    InvalidateRect(m_hwnd, nullptr, TRUE);
  }

  short escState = GetAsyncKeyState(VK_ESCAPE);
  if (escState & 0x8000) {
    CancelTabDrag();
  }
}

void ReDockItContainer::EndTabDrag(int x, int y)
{
  if (!m_dragState.active || !m_dragState.dragStarted) {
    memset(&m_dragState, 0, sizeof(m_dragState));
    m_dragState.sourcePaneId = -1;
    m_dragState.highlightPaneId = -1;
    ReleaseCapture();
    return;
  }

  int targetPane = PaneAtPoint(x, y);
  if (targetPane >= 0 && targetPane != m_dragState.sourcePaneId) {
    if (m_winMgr.GetTabCount(targetPane) < MAX_TABS_PER_PANE) {
      m_winMgr.MoveTab(m_dragState.sourcePaneId, m_dragState.sourceTabIndex, targetPane);
      RefreshLayout();
      SaveState();
    }
  }

  memset(&m_dragState, 0, sizeof(m_dragState));
  m_dragState.sourcePaneId = -1;
  m_dragState.highlightPaneId = -1;
  ReleaseCapture();
  InvalidateRect(m_hwnd, nullptr, TRUE);
}

void ReDockItContainer::CancelTabDrag()
{
  memset(&m_dragState, 0, sizeof(m_dragState));
  m_dragState.sourcePaneId = -1;
  m_dragState.highlightPaneId = -1;
  ReleaseCapture();
  InvalidateRect(m_hwnd, nullptr, TRUE);
}

// =========================================================================
// Event handlers
// =========================================================================

void ReDockItContainer::OnSize(int cx, int cy)
{
  m_tree.Recalculate(cx, cy);
  m_winMgr.RepositionAll(m_tree);
  // TODO(4.2): targeted InvalidateRect — compute dirty rect instead of full invalidate
  InvalidateRect(m_hwnd, nullptr, TRUE);
}

void ReDockItContainer::OnMouseMove(int x, int y)
{
  if (m_dragState.sourcePaneId >= 0) {
    UpdateTabDrag(x, y);
    return;
  }

  if (m_tree.IsDragging()) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_tree.Drag(x, y, rc.right - rc.left, rc.bottom - rc.top);
    m_winMgr.RepositionAll(m_tree);
    // TODO(4.2): targeted InvalidateRect — compute dirty rect instead of full invalidate
    InvalidateRect(m_hwnd, nullptr, TRUE);
    return;
  }

  // Hover highlight for splitters
  int hover = m_tree.HitTestSplitter(x, y);
  if (hover != m_hoverSplitter) {
    m_hoverSplitter = hover;
    InvalidateRect(m_hwnd, nullptr, TRUE);
    if (hover >= 0)
      SetTimer(m_hwnd, TIMER_ID_HOVER, 60, nullptr);
    else
      KillTimer(m_hwnd, TIMER_ID_HOVER);
  }

  // Hover highlight for tabs and menu button
  int hPane = -1, hTab = -1;
  for (int i = 0; i < m_tree.GetLeafCount(); i++) {
    int paneId = m_tree.GetPaneId(m_tree.GetLeafList()[i]);
    if (paneId < 0) continue;
    int t = TabHitTest(paneId, x, y);
    if (t != -1) { hPane = paneId; hTab = t; break; }
  }
  if (hPane != m_hoverPane || hTab != m_hoverTab) {
    m_hoverPane = hPane;
    m_hoverTab = hTab;
    InvalidateRect(m_hwnd, nullptr, TRUE);
    if (hTab != -1 && m_hoverSplitter < 0)
      SetTimer(m_hwnd, TIMER_ID_HOVER, 60, nullptr);
    else if (hTab == -1 && m_hoverSplitter < 0)
      KillTimer(m_hwnd, TIMER_ID_HOVER);
  }
}

void ReDockItContainer::OnLButtonUp(int x, int y)
{
  if (m_dragState.sourcePaneId >= 0) {
    EndTabDrag(x, y);
    return;
  }

  if (m_tree.IsDragging()) {
    m_tree.EndDrag();
    ReleaseCapture();
    SaveState();
  }
}

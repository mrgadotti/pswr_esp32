//*********************************************************************************
//  ListScreen.cpp
//*********************************************************************************
#include "ListScreen.h"

#include <string.h>

#include "UiApp.h"
#include "UiCallback.h"
#include "UiTheme.h"

//  Legacy list geometry (DisplayManager.cpp), unchanged.
static constexpr lv_coord_t LIST_X  = 88;
static constexpr lv_coord_t LIST_W  = 150;
static constexpr lv_coord_t LIST_Y0 = 40;
static constexpr lv_coord_t LIST_RH = 22;

ListScreen::ListScreen(const char* title, const char* const* items, int n, int startSel)
{
  strncpy(title_, title, sizeof(title_) - 1);

  count_ = (n > MAX_ITEMS) ? MAX_ITEMS : n;
  for (int i = 0; i < count_; i++) {
    strncpy(items_[i], items[i], MAX_LEN - 1);
    items_[i][MAX_LEN - 1] = 0;
  }

  sel_ = (startSel >= 0 && startSel < count_) ? startSel : 0;
}

void ListScreen::build()
{
  root_ = lv_obj_create(nullptr);
  UiTheme::styleScreen(root_);

  //  Shared style, not local properties: this is the screen the user is looking
  //  at when they switch themes, so it has to repaint without being rebuilt.
  lv_obj_t* t = lv_label_create(root_);
  lv_obj_add_style(t, &UiTheme::styleTitle, 0);
  lv_obj_set_pos(t, LIST_X, 8);
  lv_label_set_text(t, title_);

  //  The one intentionally scrollable container in the whole UI.
  listBox_ = lv_obj_create(root_);
  lv_obj_remove_style_all(listBox_);
  lv_obj_add_style(listBox_, &UiTheme::stylePanel, 0);
  lv_obj_set_pos(listBox_, LIST_X - 4, LIST_Y0);
  lv_obj_set_size(listBox_, LIST_W, lv_disp_get_ver_res(nullptr) - LIST_Y0);
  lv_obj_set_flex_flow(listBox_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(listBox_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(listBox_, LV_SCROLLBAR_MODE_OFF);

  for (int i = 0; i < count_; i++)
  {
    rows_[i] = lv_obj_create(listBox_);
    lv_obj_remove_style_all(rows_[i]);
    lv_obj_add_style(rows_[i], &UiTheme::styleRow, 0);
    lv_obj_add_style(rows_[i], &UiTheme::styleRowSelected, LV_STATE_CHECKED);
    lv_obj_set_size(rows_[i], LIST_W, LIST_RH);
    lv_obj_clear_flag(rows_[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(rows_[i], LV_OBJ_FLAG_CLICKABLE);
    lvSetIndex(rows_[i], (uint8_t)i);
    lv_obj_add_event_cb(rows_[i], lvBind<ListScreen, &ListScreen::onRow>,
                        LV_EVENT_CLICKED, this);

    lv_obj_t* l = lv_label_create(rows_[i]);
    lv_label_set_text(l, items_[i]);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  }

  nav_.create(root_, navCb, this);
  applySelection();
}

void ListScreen::applySelection()
{
  for (int i = 0; i < count_; i++) {
    if (i == sel_) lv_obj_add_state(rows_[i], LV_STATE_CHECKED);
    else           lv_obj_clear_state(rows_[i], LV_STATE_CHECKED);
  }
  if (sel_ >= 0 && sel_ < count_) lv_obj_scroll_to_view(rows_[sel_], LV_ANIM_OFF);
}

void ListScreen::onNav(uint8_t key)
{
  switch (key) {
    case NavColumn::Up:    sel_ = (sel_ - 1 + count_) % count_; applySelection(); break;
    case NavColumn::Down:  sel_ = (sel_ + 1) % count_;          applySelection(); break;
    case NavColumn::Enter: finish(sel_); break;
    default:               finish(-1);   break;
  }
}

void ListScreen::navCb(void* ctx, uint8_t key)
{
  static_cast<ListScreen*>(ctx)->onNav(key);
}

//  Tapping a row selects AND confirms it, which is what a touch user expects;
//  the nav column keeps the two steps separate for encoder-style use.
void ListScreen::onRow(lv_event_t* e)
{
  sel_ = lvIndexOf(e);
  applySelection();
  finish(sel_);
}

void ListScreen::finish(int sel)
{
  //  Runs inside an LVGL event dispatch, so it must not delete this screen.
  //  Both the sink and the pop are safe: the sink only writes settings, and
  //  requestPop() is deferred to the next UiApp::tick().
  if (doneFn_) doneFn_(doneCtx_, sel);
  if (app_) app_->requestPop();
}

//*********************************************************************************
//  AdjustScreen.cpp
//*********************************************************************************
#include "AdjustScreen.h"

#include <stdio.h>
#include <string.h>

#include "UiApp.h"
#include "UiTheme.h"

static constexpr lv_coord_t BODY_X = 88;

AdjustScreen::AdjustScreen(const char* title, const char* unit, int value,
                           int lo, int hi, int step, bool tenths, const char* topLbl)
{
  strncpy(title_, title, sizeof(title_) - 1);
  if (unit) strncpy(unit_, unit, sizeof(unit_) - 1);
  if (topLbl) {
    strncpy(topLbl_, topLbl, sizeof(topLbl_) - 1);
    hasTopLbl_ = true;
  }

  lo_     = lo;
  hi_     = hi;
  step_   = step;
  tenths_ = tenths;
  value_  = (value < lo) ? lo : (value > hi ? hi : value);
}

void AdjustScreen::build()
{
  root_ = lv_obj_create(nullptr);
  UiTheme::styleScreen(root_);

  lv_obj_t* t = lv_label_create(root_);
  lv_obj_add_style(t, &UiTheme::styleTitle, 0);
  lv_obj_set_pos(t, BODY_X, 8);
  lv_label_set_text(t, title_);

  const lv_coord_t bodyW = lv_disp_get_hor_res(nullptr) - BODY_X;

  lblValue_ = lv_label_create(root_);
  lv_obj_set_style_text_font(lblValue_, UI_FONT_BIG, 0);
  lv_obj_set_style_text_color(lblValue_, UI_YELLOW, 0);
  lv_obj_set_style_text_align(lblValue_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(lblValue_, BODY_X, 64);
  lv_obj_set_width(lblValue_, bodyW);

  if (unit_[0]) {
    lv_obj_t* u = lv_label_create(root_);
    lv_obj_set_style_text_font(u, UI_FONT_MED, 0);
    lv_obj_set_style_text_color(u, UI_GREY, 0);
    lv_obj_set_style_text_align(u, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(u, BODY_X, 132);
    lv_obj_set_width(u, bodyW);
    lv_label_set_text(u, unit_);
  }

  nav_.create(root_, navCb, this);
  refresh();
}

void AdjustScreen::refresh()
{
  char b[16];
  if (hasTopLbl_ && value_ >= hi_) snprintf(b, sizeof(b), "%s", topLbl_);
  else if (tenths_)                snprintf(b, sizeof(b), "%d.%d", value_ / 10, value_ % 10);
  else                             snprintf(b, sizeof(b), "%d", value_);

  lv_label_set_text(lblValue_, b);
}

void AdjustScreen::onNav(uint8_t key)
{
  switch (key)
  {
    case NavColumn::Up:
      value_ += step_;
      if (value_ > hi_) value_ = hi_;
      refresh();
      if (changeFn_) changeFn_(changeCtx_, value_);
      return;

    case NavColumn::Down:
      value_ -= step_;
      if (value_ < lo_) value_ = lo_;
      refresh();
      if (changeFn_) changeFn_(changeCtx_, value_);
      return;

    default:
      //  ENTER and EXIT both confirm, matching the legacy behaviour.
      if (doneFn_) doneFn_(doneCtx_, value_);
      if (app_) app_->requestPop();
      return;
  }
}

void AdjustScreen::navCb(void* ctx, uint8_t key)
{
  static_cast<AdjustScreen*>(ctx)->onNav(key);
}

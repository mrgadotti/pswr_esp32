//*********************************************************************************
//  TouchCalScreen.cpp
//*********************************************************************************
#include "TouchCalScreen.h"

#include <stdio.h>

#include "UiApp.h"
#include "UiTheme.h"

void TouchCalScreen::build()
{
  root_ = lv_obj_create(nullptr);
  UiTheme::styleScreen(root_);

  const int16_t w = (int16_t)lv_disp_get_hor_res(nullptr);
  const int16_t h = (int16_t)lv_disp_get_ver_res(nullptr);

  //  Order matters only for the user's comfort: TL, TR, BL, BR.
  tx_[0] = INSET;       ty_[0] = INSET;
  tx_[1] = w - INSET;   ty_[1] = INSET;
  tx_[2] = INSET;       ty_[2] = h - INSET;
  tx_[3] = w - INSET;   ty_[3] = h - INSET;

  lv_obj_t* title = lv_label_create(root_);
  lv_obj_set_style_text_font(title, UI_FONT_LARGE, 0);
  lv_obj_set_style_text_color(title, UI_YELLOW, 0);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(title, w);
  lv_obj_set_pos(title, 0, 84);
  lv_label_set_text(title, "Touch Calibration");

  lblHint_ = lv_label_create(root_);
  lv_obj_set_style_text_font(lblHint_, UI_FONT_MED, 0);
  lv_obj_set_style_text_color(lblHint_, UI_GREY, 0);
  lv_obj_set_style_text_align(lblHint_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(lblHint_, w);
  lv_obj_set_pos(lblHint_, 0, 118);

  //  Crosshair: two thin bars moved around rather than recreated per target.
  auto makeBar = [&](lv_coord_t bw, lv_coord_t bh) {
    lv_obj_t* o = lv_obj_create(root_);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, UI_LTBLUE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_size(o, bw, bh);
    return o;
  };
  hLine_ = makeBar(21, 1);
  vLine_ = makeBar(1, 21);

  showTarget();
}

void TouchCalScreen::showTarget()
{
  lv_obj_set_pos(hLine_, tx_[point_] - 10, ty_[point_]);
  lv_obj_set_pos(vLine_, tx_[point_], ty_[point_] - 10);

  char b[40];
  snprintf(b, sizeof(b), "Press the crosshair  (%u/%u)",
           (unsigned)(point_ + 1), (unsigned)POINTS);
  lv_label_set_text(lblHint_, b);
}

//*********************************************************************************
//  SAMPLING
//
//  Driven from onData(), i.e. the 10 ms meter tick, rather than from an LVGL
//  input event: this screen needs the RAW controller counts, and by definition
//  those are the ones LVGL's mapped indev cannot give it.
//*********************************************************************************
void TouchCalScreen::onData(const MeterReadings& r, const Settings& s)
{
  LV_UNUSED(r);
  LV_UNUSED(s);

  if (done_ || !touch_) return;

  int rx, ry;
  const bool pressed = touch_->readRaw(rx, ry);

  //  Advancing only ever happens on release, which is what stops a single long
  //  press from walking through every target.
  if (!pressed) {
    if (got_ >= SAMPLES) finishPoint();
    return;
  }

  if (got_ < SAMPLES) {
    sumX_ += rx;
    sumY_ += ry;
    got_++;
  }
}

void TouchCalScreen::finishPoint()
{
  rawX_[point_] = sumX_ / got_;
  rawY_[point_] = sumY_ / got_;

  sumX_ = sumY_ = 0;
  got_  = 0;

  if (++point_ >= POINTS) {
    done_ = true;
    solveAndStore();
    if (app_) app_->requestPop();
    return;
  }
  showTarget();
}

//*********************************************************************************
//  SOLVE
//
//  Two targets share each edge, so average them before fitting - that cancels
//  most of the panel's skew.  Then extrapolate from the inset targets out to the
//  true screen edges:
//
//      rawPerPx = (rawFar - rawNear) / (screenFar - screenNear)
//      rawAt0   = rawNear - rawPerPx * screenNear
//      rawAtMax = rawAt0  + rawPerPx * (screenSize - 1)
//*********************************************************************************
void TouchCalScreen::solveAndStore()
{
  const int16_t w = (int16_t)lv_disp_get_hor_res(nullptr);
  const int16_t h = (int16_t)lv_disp_get_ver_res(nullptr);

  const double rawLeft   = (rawX_[0] + rawX_[2]) / 2.0;
  const double rawRight  = (rawX_[1] + rawX_[3]) / 2.0;
  const double rawTop    = (rawY_[0] + rawY_[1]) / 2.0;
  const double rawBottom = (rawY_[2] + rawY_[3]) / 2.0;

  const double spanX = (double)(tx_[1] - tx_[0]);   // screen px between targets
  const double spanY = (double)(ty_[2] - ty_[0]);
  if (spanX < 1 || spanY < 1) return;

  const double perPxX = (rawRight  - rawLeft) / spanX;
  const double perPxY = (rawBottom - rawTop)  / spanY;

  const double minX = rawLeft - perPxX * tx_[0];
  const double maxX = minX + perPxX * (w - 1);
  const double minY = rawTop  - perPxY * ty_[0];
  const double maxY = minY + perPxY * (h - 1);

  Settings& st = app_->config().settings();
  st.touchMinX = (int16_t)minX;
  st.touchMaxX = (int16_t)maxX;
  st.touchMinY = (int16_t)minY;
  st.touchMaxY = (int16_t)maxY;

  //  Apply before persisting, so a bad solve is obvious immediately rather than
  //  after the next boot.  setTouchCal() rejects a degenerate span.
  touch_->setTouchCal(st.touchMinX, st.touchMaxX, st.touchMinY, st.touchMaxY);
  app_->config().saveTouchCal();
}

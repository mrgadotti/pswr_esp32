//*********************************************************************************
//  PowerBarWidget.cpp
//*********************************************************************************
#include "PowerBarWidget.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "UiTheme.h"

void PowerBarWidget::create(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                            lv_coord_t len, lv_coord_t h)
{
  len_   = len;
  h_     = h;
  lo_    = 0;
  hi_    = 0;
  drawn_ = false;

  //  One object carrying the 1 px frame; its CONTENT area is the len x h bar.
  fill_ = lv_obj_create(parent);
  lv_obj_remove_style_all(fill_);
  lv_obj_add_style(fill_, &UiTheme::styleFrame, 0);
  lv_obj_set_pos(fill_, x, y);
  lv_obj_set_size(fill_, len + 2, h + 2);
  //  NOT CLICKABLE.  lv_obj_create() sets that flag by default, and a bar that
  //  keeps it swallows the press instead of letting it reach the panel behind -
  //  which is what MeterScreen listens on to acknowledge an SWR alarm.  Labels
  //  already clear it in their own constructor; these plain objects do not.
  lv_obj_clear_flag(fill_, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(fill_, drawFillCb, LV_EVENT_DRAW_MAIN, this);

  //  Tick strip, wider than the bar so the last label can overhang.
  scale_ = lv_obj_create(parent);
  lv_obj_remove_style_all(scale_);
  lv_obj_add_style(scale_, &UiTheme::stylePanel, 0);
  lv_obj_set_pos(scale_, x - 4, y + h + 2);
  lv_obj_set_size(scale_, len + 26, SCALE_H);
  lv_obj_clear_flag(scale_, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scale_, drawScaleCb, LV_EVENT_DRAW_MAIN, this);
}

//*********************************************************************************
//  FILL
//*********************************************************************************
void PowerBarWidget::invalidateSpan(lv_coord_t a, lv_coord_t b)
{
  if (a > b) { lv_coord_t t = a; a = b; b = t; }
  if (a < 0) a = 0;
  if (b > len_ - 1) b = len_ - 1;
  if (b < a) return;

  lv_area_t area;
  lv_obj_get_content_coords(fill_, &area);
  area.x1 += a;
  area.x2  = area.x1 + (b - a);
  lv_obj_invalidate_area(fill_, &area);
}

void PowerBarWidget::setValues(double low, double high, double maxLevel)
{
  if (maxLevel <= 0) return;
  if (low  > maxLevel) low  = maxLevel;
  if (high > maxLevel) high = maxLevel;
  if (low  < 0) low  = 0;
  if (high < 0) high = 0;

  lv_coord_t lo = (lv_coord_t)(len_ * low  / maxLevel);
  lv_coord_t hi = (lv_coord_t)(len_ * high / maxLevel);
  if (hi < lo) hi = lo;

  if (drawn_ && lo == lo_ && hi == hi_) return;

  const lv_coord_t oldLo = lo_, oldHi = hi_;
  lo_ = lo;
  hi_ = hi;

  if (!drawn_) { drawn_ = true; lv_obj_invalidate(fill_); return; }

  //  Only the two moving boundaries can have changed anything.
  if (lo != oldLo) invalidateSpan(oldLo, lo);
  if (hi != oldHi) invalidateSpan(oldHi, hi);
}

void PowerBarWidget::drawFill(lv_event_t* e)
{
  lv_draw_ctx_t* ctx = lv_event_get_draw_ctx(e);
  lv_obj_t*      obj = lv_event_get_target(e);

  lv_area_t a;
  lv_obj_get_content_coords(obj, &a);

  //  Clip to the invalid span, so a one-pixel move costs one pixel column.
  lv_coord_t c0 = ctx->clip_area->x1 - a.x1;
  lv_coord_t c1 = ctx->clip_area->x2 - a.x1;
  if (c0 < 0) c0 = 0;
  if (c1 > len_ - 1) c1 = len_ - 1;
  if (c1 < c0) return;

  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_opa = LV_OPA_COVER;

  //  Three bands: yellow [0,lo), green [lo,hi), black [hi,len).
  auto band = [&](lv_coord_t from, lv_coord_t to, lv_color_t colour) {
    if (to <= from) return;
    lv_coord_t f = (from > c0) ? from : c0;
    lv_coord_t t = (to - 1 < c1) ? to - 1 : c1;
    if (t < f) return;
    dsc.bg_color = colour;
    lv_area_t r = { (lv_coord_t)(a.x1 + f), a.y1, (lv_coord_t)(a.x1 + t), a.y2 };
    lv_draw_rect(ctx, &dsc, &r);
  };

  band(0,   lo_,  UI_BAR_LIVE);
  band(lo_, hi_,  UI_GREEN);
  band(hi_, len_, UI_BAR_TROUGH);
}

void PowerBarWidget::drawFillCb(lv_event_t* e)
{
  static_cast<PowerBarWidget*>(lv_event_get_user_data(e))->drawFill(e);
}

//*********************************************************************************
//  TICK SCALE  -  1:1 port of the legacy PowerBar::drawScale().
//
//  The divisor is 11 rather than 10 when the mantissa is a multiple of 1.1,
//  which is what makes the 11/22/55 presets land on whole numbers.  The final
//  tick carries the unit string instead of a number.
//*********************************************************************************
void PowerBarWidget::setScale(double value, const char* unit)
{
  if (value == scaleVal_ && strcmp(unit, scaleUnit_) == 0) return;
  scaleVal_ = value;
  strncpy(scaleUnit_, unit, sizeof(scaleUnit_) - 1);
  scaleUnit_[sizeof(scaleUnit_) - 1] = 0;
  lv_obj_invalidate(scale_);
}

void PowerBarWidget::drawScale(lv_event_t* e)
{
  if (scaleVal_ < 0) return;

  lv_draw_ctx_t* ctx = lv_event_get_draw_ctx(e);
  lv_obj_t*      obj = lv_event_get_target(e);

  lv_area_t a;
  lv_obj_get_content_coords(obj, &a);

  //  The strip starts 4 px left of the frame; the bar's first pixel is 1 px
  //  inside the frame border.
  const lv_coord_t x0 = a.x1 + 4 + 1;
  const lv_coord_t yT = a.y1;

  lv_draw_rect_dsc_t tick;
  lv_draw_rect_dsc_init(&tick);
  tick.bg_color = UI_WHITE;
  tick.bg_opa   = LV_OPA_COVER;

  lv_draw_label_dsc_t lbl;
  lv_draw_label_dsc_init(&lbl);
  lbl.color = UI_WHITE;
  lbl.font  = UI_FONT_TICK;
  lbl.align = LV_TEXT_ALIGN_CENTER;

  const double s       = scaleVal_;
  const double divisor = (((int)(s * 10 + 0.1)) % 11 == 0) ? 11.0 : 10.0;

  auto majorTick = [&](lv_coord_t xx, const char* text) {
    lv_area_t t = { xx, yT, xx, (lv_coord_t)(yT + 4) };
    lv_draw_rect(ctx, &tick, &t);
    lv_area_t la = { (lv_coord_t)(xx - 20), (lv_coord_t)(yT + 5),
                     (lv_coord_t)(xx + 20), (lv_coord_t)(yT + SCALE_H) };
    lv_draw_label(ctx, &lbl, &la, text, nullptr);
  };

  majorTick(x0, "0");

  char b[10];
  double j = s / divisor;
  for (double i = len_ / divisor; i <= len_ + 0.5; i += len_ / divisor, j += s / divisor)
  {
    const lv_coord_t xx = x0 + (lv_coord_t)(i + 0.5);
    if (i >= len_ - 1) {
      majorTick(xx, scaleUnit_);
    } else {
      double k;
      bool sub = (modf(j, &k) > 0.05) && (modf(j, &k) < 0.95);
      if (sub) snprintf(b, sizeof(b), "%.1f", j);
      else     snprintf(b, sizeof(b), "%d", (int)(j + 0.5));
      majorTick(xx, b);
    }
  }

  for (double i = len_ / (2 * divisor); i <= len_; i += len_ / divisor)
  {
    const lv_coord_t xx = x0 + (lv_coord_t)(i + 0.5);
    lv_area_t t = { xx, yT, xx, (lv_coord_t)(yT + 2) };
    lv_draw_rect(ctx, &tick, &t);
  }
}

void PowerBarWidget::drawScaleCb(lv_event_t* e)
{
  static_cast<PowerBarWidget*>(lv_event_get_user_data(e))->drawScale(e);
}

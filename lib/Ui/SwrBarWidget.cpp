//*********************************************************************************
//  SwrBarWidget.cpp
//*********************************************************************************
#include "SwrBarWidget.h"

#include <math.h>
#include <stdio.h>

#include "UiTheme.h"

//  Legacy tick tables, unchanged.
static const double MAJOR[10] = { 1, 1.5, 2, 3, 4, 5, 6, 7, 8, 10 };
static const double MINOR[20] = { 1.1, 1.2, 1.3, 1.4, 1.6, 1.7, 1.8, 1.9, 2.2, 2.4,
                                  2.6, 2.8, 3.2, 3.4, 3.6, 3.8, 4.5, 5.5, 6.5, 9 };

lv_coord_t SwrBarWidget::pos(double v) const
{
  if (v < 1)  v = 1;
  if (v > 10) v = 10;
  return (lv_coord_t)(len_ * log10(v));
}

void SwrBarWidget::create(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                          lv_coord_t len, lv_coord_t h)
{
  len_ = len;
  h_   = h;

  g_ = o_ = w_ = 0;
  drawn_ = false;

  fill_ = lv_obj_create(parent);
  lv_obj_remove_style_all(fill_);
  lv_obj_add_style(fill_, &UiTheme::styleFrame, 0);
  lv_obj_set_pos(fill_, x, y);
  lv_obj_set_size(fill_, len + 2, h + 2);
  //  Not clickable, for the reason spelled out in PowerBarWidget::create().
  lv_obj_clear_flag(fill_, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(fill_, drawFillCb, LV_EVENT_DRAW_MAIN, this);

  scale_ = lv_obj_create(parent);
  lv_obj_remove_style_all(scale_);
  lv_obj_add_style(scale_, &UiTheme::stylePanel, 0);
  lv_obj_set_pos(scale_, x - 4, y + h + 2);
  lv_obj_set_size(scale_, len + 34, SCALE_H);
  lv_obj_clear_flag(scale_, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scale_, drawScaleCb, LV_EVENT_DRAW_MAIN, this);
}

void SwrBarWidget::invalidateSpan(lv_coord_t a, lv_coord_t b)
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

void SwrBarWidget::setValues(double mid, double alarm, double value)
{
  const lv_coord_t w = pos(value);
  const lv_coord_t o = (w < pos(alarm)) ? w : pos(alarm);
  const lv_coord_t g = (w < pos(mid))   ? w : pos(mid);

  if (drawn_ && w == w_ && o == o_ && g == g_) return;

  const lv_coord_t oldW = w_, oldO = o_, oldG = g_;
  w_ = w; o_ = o; g_ = g;

  if (!drawn_) { drawn_ = true; lv_obj_invalidate(fill_); return; }

  if (g != oldG) invalidateSpan(oldG, g);
  if (o != oldO) invalidateSpan(oldO, o);
  if (w != oldW) invalidateSpan(oldW, w);
}

void SwrBarWidget::drawFill(lv_event_t* e)
{
  lv_draw_ctx_t* ctx = lv_event_get_draw_ctx(e);
  lv_obj_t*      obj = lv_event_get_target(e);

  lv_area_t a;
  lv_obj_get_content_coords(obj, &a);

  lv_coord_t c0 = ctx->clip_area->x1 - a.x1;
  lv_coord_t c1 = ctx->clip_area->x2 - a.x1;
  if (c0 < 0) c0 = 0;
  if (c1 > len_ - 1) c1 = len_ - 1;
  if (c1 < c0) return;

  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_opa = LV_OPA_COVER;

  auto band = [&](lv_coord_t from, lv_coord_t to, lv_color_t colour) {
    if (to <= from) return;
    lv_coord_t f = (from > c0) ? from : c0;
    lv_coord_t t = (to - 1 < c1) ? to - 1 : c1;
    if (t < f) return;
    dsc.bg_color = colour;
    lv_area_t r = { (lv_coord_t)(a.x1 + f), a.y1, (lv_coord_t)(a.x1 + t), a.y2 };
    lv_draw_rect(ctx, &dsc, &r);
  };

  band(0,   g_,   UI_GREEN);
  band(g_,  o_,   UI_ORANGE);
  band(o_,  w_,   UI_RED);
  band(w_,  len_, UI_BAR_TROUGH);
}

void SwrBarWidget::drawFillCb(lv_event_t* e)
{
  static_cast<SwrBarWidget*>(lv_event_get_user_data(e))->drawFill(e);
}

void SwrBarWidget::drawScale(lv_event_t* e)
{
  lv_draw_ctx_t* ctx = lv_event_get_draw_ctx(e);
  lv_obj_t*      obj = lv_event_get_target(e);

  lv_area_t a;
  lv_obj_get_content_coords(obj, &a);

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

  char b[8];
  for (uint8_t i = 0; i < 10; i++)
  {
    const lv_coord_t xx = x0 + pos(MAJOR[i]);
    lv_area_t t = { xx, yT, xx, (lv_coord_t)(yT + 4) };
    lv_draw_rect(ctx, &tick, &t);

    lv_area_t la;
    if (MAJOR[i] < 10) {
      double k;
      bool sub = (modf(MAJOR[i], &k) > 0.05) && (modf(MAJOR[i], &k) < 0.95);
      if (sub) snprintf(b, sizeof(b), "%.1f", MAJOR[i]);
      else     snprintf(b, sizeof(b), "%d", (int)(MAJOR[i] + 0.5));

      lbl.align = LV_TEXT_ALIGN_CENTER;
      la = { (lv_coord_t)(xx - 20), (lv_coord_t)(yT + 5),
             (lv_coord_t)(xx + 20), (lv_coord_t)(yT + SCALE_H) };
    } else {
      //  The legacy scale labels the top of the axis "SWR", not "10", and it
      //  did so RIGHT-aligned (TR_DATUM) ending at the tick.  That is not a
      //  style choice: the last tick sits at x ~= 310 on a 320 px panel, so a
      //  centred "SWR" runs off the right edge and loses the R.
      snprintf(b, sizeof(b), "SWR");

      lbl.align = LV_TEXT_ALIGN_RIGHT;
      la = { (lv_coord_t)(xx - 40), (lv_coord_t)(yT + 5),
             (lv_coord_t)(xx + 1),  (lv_coord_t)(yT + SCALE_H) };
    }
    lv_draw_label(ctx, &lbl, &la, b, nullptr);
  }

  for (uint8_t i = 0; i < 20; i++)
  {
    const lv_coord_t xx = x0 + pos(MINOR[i]);
    lv_area_t t = { xx, yT, xx, (lv_coord_t)(yT + 2) };
    lv_draw_rect(ctx, &tick, &t);
  }
}

void SwrBarWidget::drawScaleCb(lv_event_t* e)
{
  static_cast<SwrBarWidget*>(lv_event_get_user_data(e))->drawScale(e);
}

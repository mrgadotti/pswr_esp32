//*********************************************************************************
//  ScopeWidget.cpp
//*********************************************************************************
#include "ScopeWidget.h"

#include "UiTheme.h"

//  Trace colours, in the same roles the original used (C_GREEN body, C_YELLOW
//  envelope tips) but taken from the live palette rather than frozen here, so
//  the scope follows a theme change like every other widget.  These were
//  hardcoded, which left a black box with a pure-green trace sitting in the
//  middle of whatever theme was actually selected.
//
//  The body/tip pair reads at DRAW time and so follows for free; the background
//  and frame are applied at build time and follow because MeterScreen rebuilds
//  its contents in onEnter(), which is exactly what returning from the menu does.
#define COL_BODY  UI_GREEN
#define COL_TIP   UI_BAR_LIVE
#define COL_BG    UI_BLACK
#define COL_FRM   UI_WHITE

void ScopeWidget::create(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                         lv_coord_t len, lv_coord_t h)
{
  len_ = (len > SCOPE_BUF) ? SCOPE_BUF : len;
  h_   = h;
  mid_ = h / 2;
  max_ = h / 2;
  in_  = 0;
  for (int16_t i = 0; i < SCOPE_BUF; i++) vals_[i] = 0;

  obj_ = lv_obj_create(parent);
  lv_obj_set_pos(obj_, x, y);
  lv_obj_set_size(obj_, len_ + 2, h_ + 2);

  //  Opaque background is load-bearing: with a transparent one, every column
  //  invalidation would force the parent to repaint that strip too.
  lv_obj_set_style_bg_color(obj_, COL_BG, 0);
  lv_obj_set_style_bg_opa(obj_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(obj_, COL_FRM, 0);
  lv_obj_set_style_border_width(obj_, 1, 0);
  lv_obj_set_style_radius(obj_, 0, 0);
  lv_obj_set_style_pad_all(obj_, 0, 0);
  //  Not clickable, for the reason spelled out in PowerBarWidget::create() - and
  //  it matters most here, since the scope covers nearly the whole content area.
  lv_obj_clear_flag(obj_, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

  lv_obj_add_event_cb(obj_, drawCb, LV_EVENT_DRAW_MAIN, this);
}

void ScopeWidget::add(double level, double fullscale)
{
  if (fullscale < level) fullscale = level;
  if (fullscale < 0.05)  fullscale = 0.05;

  int16_t v = (int16_t)(level / fullscale * max_);
  if (v < 0)     v = 0;
  if (v > max_)  v = max_;

  vals_[in_] = v;

  //  Invalidate exactly one column.  LVGL accumulates and joins invalid areas
  //  internally between refreshes, so there is no need to track a dirty span
  //  here - but there IS a need to never widen this area casually.
  lv_area_t col;
  lv_obj_get_content_coords(obj_, &col);
  col.x1 += in_;
  col.x2  = col.x1;
  lv_obj_invalidate_area(obj_, &col);

  if (++in_ >= len_) in_ = 0;
}

//*********************************************************************************
//  DRAW
//
//  Renders the mirrored envelope for the columns that intersect the clip area -
//  and ONLY those.  Iterating all 304 columns and letting LVGL reject the ones
//  outside the clip would mean 304 draw calls, each with its own clip test, at
//  100 Hz.  Clipping here is what keeps a one-column update to one draw call.
//*********************************************************************************
void ScopeWidget::draw(lv_event_t* e)
{
  lv_draw_ctx_t* ctx = lv_event_get_draw_ctx(e);
  lv_obj_t*      obj = lv_event_get_target(e);

  lv_area_t content;
  lv_obj_get_content_coords(obj, &content);

  int32_t c0 = ctx->clip_area->x1 - content.x1;
  int32_t c1 = ctx->clip_area->x2 - content.x1;
  if (c0 < 0)     c0 = 0;
  if (c1 > len_ - 1) c1 = len_ - 1;
  if (c1 < c0)    return;

  lv_draw_rect_dsc_t body;
  lv_draw_rect_dsc_init(&body);
  body.bg_color = COL_BODY;
  body.bg_opa   = LV_OPA_COVER;

  lv_draw_rect_dsc_t tip;
  lv_draw_rect_dsc_init(&tip);
  tip.bg_color = COL_TIP;
  tip.bg_opa   = LV_OPA_COVER;

  const lv_coord_t yc = content.y1 + mid_;

  for (int32_t c = c0; c <= c1; c++)
  {
    const lv_coord_t xx = content.x1 + (lv_coord_t)c;
    const int16_t    v  = vals_[c];

    if (v <= 0) {
      //  Flat line: a single lit pixel on the centreline.
      lv_area_t a = { xx, yc, xx, yc };
      lv_draw_rect(ctx, &tip, &a);
      continue;
    }

    lv_area_t bodyArea = { xx, (lv_coord_t)(yc - v + 1), xx, (lv_coord_t)(yc + v - 1) };
    lv_draw_rect(ctx, &body, &bodyArea);

    lv_area_t top = { xx, (lv_coord_t)(yc - v), xx, (lv_coord_t)(yc - v) };
    lv_draw_rect(ctx, &tip, &top);

    lv_area_t bot = { xx, (lv_coord_t)(yc + v), xx, (lv_coord_t)(yc + v) };
    lv_draw_rect(ctx, &tip, &bot);
  }
}

void ScopeWidget::drawCb(lv_event_t* e)
{
  static_cast<ScopeWidget*>(lv_event_get_user_data(e))->draw(e);
}

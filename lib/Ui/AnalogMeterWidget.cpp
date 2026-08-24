//*********************************************************************************
//  AnalogMeterWidget.cpp
//*********************************************************************************
#include "AnalogMeterWidget.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "UiTheme.h"

//*********************************************************************************
//  GEOMETRY
//
//  LVGL's draw-primitive angle convention (see lv_draw_sw_arc.c): 0 deg sits at
//  3 o'clock and increases CLOCKWISE, so x = cx + r*cos(a), y = cy + r*sin(a)
//  with a in degrees.  A 150 deg sweep centred on 270 deg (12 o'clock) puts "0"
//  at 195 deg (upper-left, past 9 o'clock) and full-scale at 345 deg
//  (upper-right, short of 3 o'clock) - the same resting-left, swinging-right
//  throw a Bird Thruline element's movement has.
//*********************************************************************************
static constexpr double ANGLE_START = 195.0;
static constexpr double ANGLE_END   = 345.0;
static constexpr double ANGLE_SPAN  = ANGLE_END - ANGLE_START;
static constexpr double DEG2RAD     = 3.14159265358979323846 / 180.0;

//  Local geometry margins, in pixels - see create() for how they turn a
//  bounding box into a pivot point and a set of radii.
static constexpr lv_coord_t PIVOT_BOTTOM = 22;   // pivot height above the box's bottom edge
static constexpr lv_coord_t LABEL_OUT    = 11;   // rLabel_ - rOuter_ (kept in one place: see rLabel_ below)
static constexpr lv_coord_t LABEL_HALF_H = 7;    // half the tick-label box height (also used in drawFace)
static constexpr lv_coord_t TOP_MARGIN   = 4;    // breathing room above the topmost label

//  The apex tick label (12 o'clock, the tightest case) sits at radius
//  rLabel_ = rOuter_ + LABEL_OUT, itself LABEL_HALF_H tall each way.  Reserving
//  clearance for rOuter_ alone - what an earlier version of this file did -
//  left the label's own overshoot unaccounted for, and on real hardware it
//  clipped clean off the top of the panel.  Deriving the reservation from the
//  SAME constants drawFace() uses to place the label is what keeps the two in
//  sync; a magic top-margin number that isn't tied to them is how this broke
//  the first time.
static constexpr lv_coord_t TOP_CLEARANCE = LABEL_OUT + LABEL_HALF_H + TOP_MARGIN;

static constexpr lv_coord_t LABEL_PAD    = 14;   // clearance beyond the 0/FS tick labels
static constexpr double     COS_END      = 0.9659258262890683;   // cos(15 deg)

void AnalogMeterWidget::create(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                               lv_coord_t w, lv_coord_t h)
{
  w_ = w;
  h_ = h;

  scaleVal_     = -1;
  scaleUnit_[0] = 0;
  fraction_     = 0;
  drawn_        = false;

  //  Radius is the tighter of what the width allows (the arc's horizontal
  //  half-throw at its ends is r*cos(15 deg)) and what the height allows (the
  //  apex tick label's outer edge, straight up from the pivot, must still land
  //  inside the box - see TOP_CLEARANCE above).
  double rw = (w_ / 2.0 - LABEL_PAD) / COS_END;
  double rh = (double)(h_ - PIVOT_BOTTOM - TOP_CLEARANCE);
  double r  = (rw < rh) ? rw : rh;
  if (r < 20) r = 20;      // degenerate box - draw something rather than nothing

  rOuter_   = (lv_coord_t)r;
  rTickIn_  = (lv_coord_t)(r - 9);
  rMinorIn_ = (lv_coord_t)(r - 5);
  rLabel_   = (lv_coord_t)(r + LABEL_OUT);
  //  The needle reaches PAST the tick tips, the way a Bird Thruline or a
  //  multimeter's pointer runs up to (and a little over) the scale arc rather
  //  than stopping short of it with a gap.  Still well clear of rLabel_, so it
  //  never runs into the numbers themselves.
  rNeedle_  = (lv_coord_t)(r + 4);
  rTail_    = 13;

  cx_ = w_ / 2;
  cy_ = h_ - PIVOT_BOTTOM;

  tipPx_ = tipLocal(0.0);

  obj_ = lv_obj_create(parent);
  lv_obj_remove_style_all(obj_);
  lv_obj_set_pos(obj_, x, y);
  lv_obj_set_size(obj_, w_, h_);
  //  Opaque canvas fill - load-bearing, same reason as ScopeWidget: without it
  //  every needle-sized invalidation would force the parent to repaint too.
  lv_obj_set_style_bg_color(obj_, UI_BLACK, 0);
  lv_obj_set_style_bg_opa(obj_, LV_OPA_COVER, 0);
  //  Not clickable - see PowerBarWidget::create() - so a tap here still reaches
  //  the content panel behind it and clears a latched SWR alarm.
  lv_obj_clear_flag(obj_, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(obj_, drawCb, LV_EVENT_DRAW_MAIN, this);
}

//*********************************************************************************
//  SCALE  -  full redraw only when the mantissa/unit actually changes.
//*********************************************************************************
void AnalogMeterWidget::setScale(double value, const char* unit)
{
  if (value == scaleVal_ && strcmp(unit, scaleUnit_) == 0) return;
  scaleVal_ = value;
  strncpy(scaleUnit_, unit, sizeof(scaleUnit_) - 1);
  scaleUnit_[sizeof(scaleUnit_) - 1] = 0;
  lv_obj_invalidate(obj_);
}

//*********************************************************************************
//  NEEDLE
//*********************************************************************************
lv_point_t AnalogMeterWidget::tipLocal(double frac) const
{
  const double angle = ANGLE_START + ANGLE_SPAN * frac;
  const double rad    = angle * DEG2RAD;
  return { (lv_coord_t)lround(cx_ + rNeedle_ * cos(rad)),
           (lv_coord_t)lround(cy_ + rNeedle_ * sin(rad)) };
}

void AnalogMeterWidget::setValue(double valueMw, double fullscaleMw, bool alarm)
{
  if (fullscaleMw <= 0) return;
  double v = valueMw;
  if (v < 0) v = 0;
  if (v > fullscaleMw) v = fullscaleMw;
  const double frac = v / fullscaleMw;

  const lv_point_t tip = tipLocal(frac);

  //  Same guard as PowerBarWidget's lo_==lo_ check: a change too small to move
  //  a pixel costs nothing - unless the alarm colour flipped, which repaints
  //  the same needle a different colour.
  const bool alarmChanged = drawn_ && (alarm != alarm_);
  if (drawn_ && !alarmChanged && tip.x == tipPx_.x && tip.y == tipPx_.y) {
    fraction_ = frac;
    return;
  }

  const lv_point_t oldTip   = tipPx_;
  const bool       wasDrawn = drawn_;
  tipPx_    = tip;
  fraction_ = frac;
  alarm_    = alarm;

  if (!wasDrawn) { drawn_ = true; lv_obj_invalidate(obj_); return; }

  //  oldTip == newTip on a pure colour change - invalidateNeedle() still
  //  covers exactly the needle+tail area that needs repainting either way.
  invalidateNeedle(oldTip, tip);
}

void AnalogMeterWidget::invalidateNeedle(const lv_point_t& oldTip, const lv_point_t& newTip)
{
  lv_area_t content;
  lv_obj_get_content_coords(obj_, &content);

  const lv_coord_t pad     = 4;               // needle line width/2 + slop
  const lv_coord_t tailPad = rTail_ + pad;    // the tail always fits this box

  lv_coord_t x1 = cx_ - tailPad, x2 = cx_ + tailPad;
  lv_coord_t y1 = cy_ - tailPad, y2 = cy_ + tailPad;

  auto grow = [&](const lv_point_t& p) {
    if (p.x - pad < x1) x1 = p.x - pad;
    if (p.x + pad > x2) x2 = p.x + pad;
    if (p.y - pad < y1) y1 = p.y - pad;
    if (p.y + pad > y2) y2 = p.y + pad;
  };
  grow(oldTip);
  grow(newTip);

  lv_area_t box = { (lv_coord_t)(content.x1 + x1), (lv_coord_t)(content.y1 + y1),
                    (lv_coord_t)(content.x1 + x2), (lv_coord_t)(content.y1 + y2) };
  lv_obj_invalidate_area(obj_, &box);
}

void AnalogMeterWidget::drawNeedle(lv_draw_ctx_t* ctx, const lv_point_t& c) const
{
  const double angle = ANGLE_START + ANGLE_SPAN * fraction_;
  const double rad    = angle * DEG2RAD;
  const double cosA = cos(rad), sinA = sin(rad);

  const lv_point_t tip  = { (lv_coord_t)lround(c.x + rNeedle_ * cosA),
                            (lv_coord_t)lround(c.y + rNeedle_ * sinA) };
  const lv_point_t tail = { (lv_coord_t)lround(c.x - rTail_ * cosA),
                            (lv_coord_t)lround(c.y - rTail_ * sinA) };

  //  UI_READING, not UI_BAR_LIVE: the bar's vivid yellow only stays legible
  //  against the grey trough the light theme repaints behind it (see the
  //  LIGHT palette notes in UiTheme.cpp) - on the plain canvas this widget
  //  draws on, that same yellow is ~1:1 contrast on a white face.  UI_READING
  //  is the role built for "the number this instrument shows" against canvas
  //  in every theme, which is exactly what the needle tip is.
  lv_draw_line_dsc_t needle;
  lv_draw_line_dsc_init(&needle);
  needle.color       = alarm_ ? UI_RED : UI_READING;
  needle.width       = 3;
  needle.round_start = 1;
  needle.round_end   = 1;
  lv_draw_line(ctx, &needle, &tail, &tip);

  lv_draw_rect_dsc_t pivot;
  lv_draw_rect_dsc_init(&pivot);
  pivot.radius   = LV_RADIUS_CIRCLE;
  pivot.bg_opa   = LV_OPA_COVER;
  pivot.bg_color = UI_WHITE;
  const lv_area_t pa = { (lv_coord_t)(c.x - 4), (lv_coord_t)(c.y - 4),
                         (lv_coord_t)(c.x + 4), (lv_coord_t)(c.y + 4) };
  lv_draw_rect(ctx, &pivot, &pa);
}

//*********************************************************************************
//  FACE  -  bezel arc, major/minor ticks and the FWD/WATTS legend.
//
//  Tick math is PowerBarWidget::drawScale() ported from linear x to polar angle:
//  same 10/11-divisor choice (so the 11/22/55 autoscale presets land on whole
//  numbers), same "last tick carries the unit string" rule.
//*********************************************************************************
void AnalogMeterWidget::drawFace(lv_draw_ctx_t* ctx, const lv_point_t& c) const
{
  lv_draw_arc_dsc_t bezel;
  lv_draw_arc_dsc_init(&bezel);
  bezel.color = UI_WHITE;
  bezel.width = 2;
  lv_draw_arc(ctx, &bezel, &c, rOuter_, (uint16_t)ANGLE_START, (uint16_t)ANGLE_END);

  lv_draw_label_dsc_t cap;
  lv_draw_label_dsc_init(&cap);
  cap.color = UI_GREY;
  cap.align = LV_TEXT_ALIGN_CENTER;

  const lv_coord_t capY = (lv_coord_t)(c.y - rOuter_ * 0.42);
  cap.font = UI_FONT_SMALL;
  lv_area_t l1 = { (lv_coord_t)(c.x - 40), (lv_coord_t)(capY - 15),
                   (lv_coord_t)(c.x + 40), (lv_coord_t)(capY + 1) };
  lv_draw_label(ctx, &cap, &l1, "FWD", nullptr);
  cap.font = UI_FONT_MED;
  lv_area_t l2 = { (lv_coord_t)(c.x - 40), (lv_coord_t)(capY + 1),
                   (lv_coord_t)(c.x + 40), (lv_coord_t)(capY + 19) };
  lv_draw_label(ctx, &cap, &l2, "WATTS", nullptr);

  if (scaleVal_ < 0) return;      // no reading yet - skip ticks, not the legend

  lv_draw_line_dsc_t major;
  lv_draw_line_dsc_init(&major);
  major.color = UI_WHITE;
  major.width = 2;

  lv_draw_line_dsc_t minor;
  lv_draw_line_dsc_init(&minor);
  minor.color = UI_GREY;
  minor.width = 1;

  lv_draw_label_dsc_t lbl;
  lv_draw_label_dsc_init(&lbl);
  lbl.color = UI_WHITE;
  lbl.font  = UI_FONT_TICK;
  lbl.align = LV_TEXT_ALIGN_CENTER;

  const double s        = scaleVal_;
  const double divisor  = (((int)(s * 10 + 0.1)) % 11 == 0) ? 11.0 : 10.0;
  const int    n        = (int)divisor;

  char b[10];
  for (int k = 0; k <= n; k++)
  {
    const double frac  = (double)k / divisor;
    const double angle = ANGLE_START + ANGLE_SPAN * frac;
    const double rad    = angle * DEG2RAD;
    const double cosA = cos(rad), sinA = sin(rad);

    const lv_point_t p1 = { (lv_coord_t)lround(c.x + rTickIn_ * cosA),
                            (lv_coord_t)lround(c.y + rTickIn_ * sinA) };
    const lv_point_t p2 = { (lv_coord_t)lround(c.x + rOuter_ * cosA),
                            (lv_coord_t)lround(c.y + rOuter_ * sinA) };
    lv_draw_line(ctx, &major, &p1, &p2);

    const char* text;
    if (k == n) {
      text = scaleUnit_;
    } else if (k == 0) {
      text = "0";
    } else {
      const double val = s * k / divisor;
      double ip;
      const double fp = modf(val, &ip);
      if (fp > 0.05 && fp < 0.95) snprintf(b, sizeof(b), "%.1f", val);
      else                        snprintf(b, sizeof(b), "%d", (int)(val + 0.5));
      text = b;
    }

    const lv_point_t lp = { (lv_coord_t)lround(c.x + rLabel_ * cosA),
                            (lv_coord_t)lround(c.y + rLabel_ * sinA) };
    const lv_area_t la = { (lv_coord_t)(lp.x - 16), (lv_coord_t)(lp.y - LABEL_HALF_H),
                           (lv_coord_t)(lp.x + 16), (lv_coord_t)(lp.y + LABEL_HALF_H) };
    lv_draw_label(ctx, &lbl, &la, text, nullptr);

    if (k < n) {
      const double mfrac  = ((double)k + 0.5) / divisor;
      const double mangle = ANGLE_START + ANGLE_SPAN * mfrac;
      const double mrad    = mangle * DEG2RAD;
      const lv_point_t m1 = { (lv_coord_t)lround(c.x + rMinorIn_ * cos(mrad)),
                              (lv_coord_t)lround(c.y + rMinorIn_ * sin(mrad)) };
      const lv_point_t m2 = { (lv_coord_t)lround(c.x + rOuter_ * cos(mrad)),
                              (lv_coord_t)lround(c.y + rOuter_ * sin(mrad)) };
      lv_draw_line(ctx, &minor, &m1, &m2);
    }
  }
}

//*********************************************************************************
//  DRAW  -  face then needle, clipped to whatever LVGL invalidated.  Every draw
//  primitive below rejects geometry outside ctx->clip_area on its own, so a
//  needle-sized clip costs a needle-sized amount of work even though this
//  function always issues the full set of calls.
//*********************************************************************************
void AnalogMeterWidget::draw(lv_event_t* e)
{
  lv_draw_ctx_t* ctx = lv_event_get_draw_ctx(e);
  lv_obj_t*      obj = lv_event_get_target(e);

  lv_area_t a;
  lv_obj_get_content_coords(obj, &a);
  const lv_point_t c = { (lv_coord_t)(a.x1 + cx_), (lv_coord_t)(a.y1 + cy_) };

  drawFace(ctx, c);
  drawNeedle(ctx, c);
}

void AnalogMeterWidget::drawCb(lv_event_t* e)
{
  static_cast<AnalogMeterWidget*>(lv_event_get_user_data(e))->draw(e);
}

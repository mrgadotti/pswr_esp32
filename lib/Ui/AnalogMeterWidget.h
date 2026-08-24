//*********************************************************************************
//  AnalogMeterWidget.h  -  A Bird Thruline-style needle gauge for forward power.
//
//  Custom-drawn, like every other live readout in this UI (see PowerBarWidget,
//  SwrBarWidget, ScopeWidget): one lv_obj_t with an LV_EVENT_DRAW_MAIN handler
//  that renders the face (arc, ticks, tick labels, the FWD/WATTS legend) AND the
//  needle every time it fires.  What keeps that cheap is the same trick those
//  widgets use - setValue() invalidates only the small area the needle actually
//  sweeps between the old and new reading, not the whole gauge, so a redraw at
//  the bar-widget's 50 Hz cadence costs a needle-sized rectangle rather than a
//  ~300x150 px face.  The face is only ever invalidated in full on the first
//  draw and when the autoscale range changes (setScale()), same as the bar
//  widget's tick strip.
//
//  COLOURS come from UiTheme's canvas/ink roles (UI_BLACK/UI_WHITE/UI_GREY),
//  baked in at create() time - which is why MeterScreen re-creates this widget
//  from buildContent() on every mode entry, exactly like ScopeWidget.  That is
//  what makes a dark-theme meter render on a black face and a light-theme one on
//  a white face: the widget itself has no theme awareness at all.
//
//  SCALE is the mantissa+unit pair MeterScreen already computes for the power
//  bars (UiFormat::scalePowerMeter over the same AutoScale hold), reusing the
//  legacy 10/11-divisor tick math from PowerBarWidget::drawScale so the numbers
//  on this gauge read the same way as everywhere else in the UI.
//*********************************************************************************
#pragma once

#include <lvgl.h>

class AnalogMeterWidget {
public:
  //  x/y/w/h is the whole gauge's bounding box: arc, ticks, legend, needle and
  //  pivot all live inside it.  The pivot sits near the bottom edge, Bird-style.
  void create(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
              lv_coord_t w, lv_coord_t h);

  //  Full-scale value + unit for the tick labels, e.g. (5.0, "W").  Cheap and
  //  idempotent, like PowerBarWidget::setScale() - a no-op unless it changed.
  void setScale(double fsValue, const char* unit);

  //  Needle position: valueMw against fullscaleMw (both mW), clamped to the
  //  scale.  fullscaleMw is the SAME value passed to setScale() in W/mW form -
  //  callers push both from one AutoScale::push() result, as MeterScreen does.
  //  `alarm` colours the needle the same way every other mode colours its
  //  headline reading (UI_READING normally, UI_RED while an SWR alarm is
  //  latched) - see MeterScreen::updateText()'s lblBig_ for the convention.
  void setValue(double valueMw, double fullscaleMw, bool alarm);

private:
  static void drawCb(lv_event_t* e);
  void        draw(lv_event_t* e);
  void        drawFace(lv_draw_ctx_t* ctx, const lv_point_t& c) const;
  void        drawNeedle(lv_draw_ctx_t* ctx, const lv_point_t& c) const;

  //  Needle tip, in OBJECT-LOCAL pixels (relative to the content area's
  //  top-left) - same convention PowerBarWidget uses for lo_/hi_, so the caller
  //  never has to know the widget's absolute screen position.
  lv_point_t tipLocal(double frac) const;
  void       invalidateNeedle(const lv_point_t& oldTip, const lv_point_t& newTip);

  lv_obj_t* obj_ = nullptr;

  lv_coord_t w_ = 0, h_ = 0;

  //  Pivot position and the five radii that lay out the face, all object-local.
  lv_coord_t cx_ = 0, cy_ = 0;
  lv_coord_t rOuter_ = 0;      // bezel arc / major tick tip
  lv_coord_t rTickIn_ = 0;     // major tick root
  lv_coord_t rMinorIn_ = 0;    // minor tick root
  lv_coord_t rLabel_ = 0;      // tick number anchor
  lv_coord_t rNeedle_ = 0;     // needle tip
  lv_coord_t rTail_ = 0;       // counterweight tail, opposite the tip

  //  Last drawn needle reading.  fraction_ feeds the draw handler; tipPx_ is
  //  its rounded pixel position, kept so setValue() can skip a redraw when a
  //  sub-pixel change would draw nothing new - PowerBarWidget's lo_==lo_ check,
  //  same idea.
  double     fraction_ = 0;
  lv_point_t tipPx_    = { 0, 0 };
  bool       alarm_    = false;
  bool       drawn_    = false;

  double scaleVal_     = -1;
  char   scaleUnit_[6] = { 0 };
};

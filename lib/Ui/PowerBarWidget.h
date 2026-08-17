//*********************************************************************************
//  PowerBarWidget.h  -  Autoscaling power bar with a two-tone fill.
//
//  FILL: one custom-drawn object, invalidated only where the fill actually
//  moved.
//
//  This is the second design.  The first used two overlapping lv_obj children
//  whose widths were set per frame, which is the obvious LVGL way and reads
//  nicely - but lv_obj_set_width() invalidates the WHOLE object, so a bar that
//  crept one pixel repainted all 300x28 of itself.  Measured on hardware with a
//  moving signal, the bars alone were pushing ~420,000 px/s and dragging the ui
//  task from its 5 ms period out to 22 ms.
//
//  So the fill is drawn in an LV_EVENT_DRAW_MAIN handler and setValues()
//  invalidates only the span between the old and new boundaries - typically a
//  few pixels wide instead of three hundred.  Same technique as ScopeWidget, and
//  the same warning applies: lv_obj_invalidate() on this object instead of
//  lv_obj_invalidate_area() puts the cost straight back.
//
//  Colours: yellow 0..inst, green inst..pep (the PEP overshoot tail), black
//  beyond - matching the legacy PowerBar::graph().
//
//  SCALE: a separate custom-drawn strip below the bar, redrawn only when the
//  autoscale changes decade (the legacy cache key: mantissa + unit string).
//*********************************************************************************
#pragma once

#include <lvgl.h>

class PowerBarWidget {
public:
  //  x/y is the top-left of the frame; len/h are the INNER bar area.
  void create(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
              lv_coord_t len, lv_coord_t h);

  //  Cheap and idempotent: returns immediately unless mantissa or unit changed.
  void setScale(double value, const char* unit);

  //  low = instantaneous (yellow), high = PEP (green tail), against maxLevel.
  void setValues(double low, double high, double maxLevel);

  static constexpr lv_coord_t SCALE_H = 14;

private:
  static void drawFillCb(lv_event_t* e);
  static void drawScaleCb(lv_event_t* e);
  void        drawFill(lv_event_t* e);
  void        drawScale(lv_event_t* e);

  //  Invalidate the horizontal span [a, b] of the fill area, in bar-local px.
  void invalidateSpan(lv_coord_t a, lv_coord_t b);

  lv_obj_t* fill_  = nullptr;   // the bar interior; owns the DRAW_MAIN handler
  lv_obj_t* scale_ = nullptr;

  lv_coord_t len_ = 0, h_ = 0;

  //  Current boundaries, in bar-local pixels.  -1 means "never drawn".
  lv_coord_t lo_ = 0, hi_ = 0;
  bool       drawn_ = false;

  double scaleVal_     = -1;
  char   scaleUnit_[6] = { 0 };
};

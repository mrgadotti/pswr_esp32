//*********************************************************************************
//  SwrBarWidget.h  -  SWR bar on a LOGARITHMIC axis, with three colour zones.
//
//  pos(v) = len * log10(clamp(v, 1, 10)), ported verbatim.  That alone rules out
//  lv_bar, whose min/max mapping is linear: you would have to pre-transform the
//  value, at which point lv_bar contributes nothing.
//
//  FILL: one custom-drawn object with three colour bands - green up to `mid`,
//  orange from there to `alarm`, red beyond - invalidated only where the fill
//  moved.  See PowerBarWidget for why this is not three lv_obj children whose
//  widths get set per frame: lv_obj_set_width() invalidates the whole object, so
//  a bar creeping one pixel repainted its full width, 50 times a second.
//
//  SCALE: drawn once (the axis is fixed), with the legacy major/minor tick
//  tables kept as-is - including the last mark rendering "SWR" instead of "10".
//*********************************************************************************
#pragma once

#include <lvgl.h>

class SwrBarWidget {
public:
  void create(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
              lv_coord_t len, lv_coord_t h);

  //  mid   = green/orange boundary (min(2.0, alarm))
  //  alarm = orange/red boundary (the SWR alarm threshold, or 10.0 if off)
  void setValues(double mid, double alarm, double value);

  static constexpr lv_coord_t SCALE_H = 14;

private:
  static void drawScaleCb(lv_event_t* e);
  void        drawScale(lv_event_t* e);

  lv_coord_t pos(double v) const;

  static void drawFillCb(lv_event_t* e);
  void        drawFill(lv_event_t* e);
  void        invalidateSpan(lv_coord_t a, lv_coord_t b);

  lv_obj_t* fill_  = nullptr;
  lv_obj_t* scale_ = nullptr;

  lv_coord_t len_ = 0, h_ = 0;

  //  Band boundaries in bar-local pixels: green [0,g), orange [g,o), red [o,w).
  lv_coord_t g_ = 0, o_ = 0, w_ = 0;
  bool       drawn_ = false;
};

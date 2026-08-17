//*********************************************************************************
//  ScopeWidget.h  -  Modulation scope: a wrapping raster sweep, LVGL edition.
//
//  This is NOT a scrolling waveform.  Like an analogue scope, one column is
//  written per meter tick and the write index wraps, so the trace is overwritten
//  in place: 300 columns at METER_TICK_MS = one sweep every 3 s.
//
//  Port note - what changed versus the TFT_eSPI original (ModulationScope):
//
//  The original kept TWO arrays, new_[] and old_[], and repainted differentially
//  per column: green over whatever the envelope had just exposed, black over
//  whatever it had vacated.  Clever, and its cost was proportional to the CHANGE
//  in amplitude rather than to the trace size - but it also meant the widget
//  believed it owned those pixels forever.  Anything that overdrew the scope
//  area (returning from the menu, a status line) corrupted the trace
//  permanently, because unchanged columns were never repainted.
//
//  In a retained-mode toolkit that bookkeeping is not just unnecessary, it is
//  wrong: LVGL repaints the background of an invalid area before calling the
//  draw handler, so "erase where it shrank" is automatic.  old_[] and the drain
//  loop are therefore GONE, and the latent corruption bug goes with them.
//
//  What replaces them is one rule: invalidate ONLY the column that changed.
//  lv_obj_invalidate() on the whole widget would repaint 300 x 150 px at 100 Hz
//  - roughly 20x the SPI budget.  The failure mode moved from "structurally
//  impossible" to "one function call away", which is why it is spelled out here.
//
//  MEASURED, and knowingly accepted (Etapa 3 gate, on hardware):
//    100 columns/s written, 1.39 ms average lv_timer_handler(), 3.6 ms worst.
//  But ~132,000 px/s reach the panel, against the ~15,200 px/s that 100 columns
//  of 152 px actually contain - an 8.6x area amplification somewhere in LVGL's
//  invalidate/join path that was isolated to this layer but NOT explained.  The
//  leading suspicion is lv_refr_join_area() merging non-adjacent invalid areas
//  into their bounding box, which would be worst at the sweep wrap (column 0 and
//  column 299 dirty at once); that is a hypothesis, not a finding.
//
//  Accepted because the absolute cost fits the 5 ms frame budget with room to
//  spare.  Revisit here first if MeterScreen ever runs short of budget.
//*********************************************************************************
#pragma once

#include <lvgl.h>

class ScopeWidget {
public:
  //  Create the widget as a child of `parent`.  len/h are the INNER trace area;
  //  the object itself is one pixel larger on each side for the frame.
  void create(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
              lv_coord_t len, lv_coord_t h);

  //  Append one column.  Call exactly once per meter tick: the sweep period is
  //  len * METER_TICK_MS, so the caller's cadence IS the time base.
  void add(double level, double fullscale);

  lv_obj_t* obj() const { return obj_; }

private:
  static void drawCb(lv_event_t* e);
  void        draw(lv_event_t* e);

  static constexpr int16_t SCOPE_BUF = 304;

  lv_obj_t* obj_ = nullptr;
  int16_t   vals_[SCOPE_BUF] = { 0 };   // half-amplitude per column, in pixels
  int16_t   len_ = 0, h_ = 0, mid_ = 0, max_ = 0;
  int16_t   in_  = 0;                   // write index, wraps at len_
};

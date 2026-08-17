//*********************************************************************************
//  UiFormat.h  -  Number formatting and autoscale, lifted out of DisplayManager.
//
//  These were already pure logic; they only ever lived in DisplayManager because
//  they wrote into a shared fmtBuf_ member.  Pulled out as free functions with a
//  caller-supplied buffer, they carry no LVGL and no TFT_eSPI dependency at all,
//  which makes them the one part of this UI that can be unit-tested on the host
//  (`pio test -e native`).  They also encode the physics - unit decades, the
//  dBm conversion, the autoscale hold - so they are the part most worth testing.
//
//  Behaviour is a faithful port: same rounding, same thresholds, same unit
//  table.  Do not "improve" the formatting without checking it against the
//  original in legacy/main_original.cpp.
//*********************************************************************************
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Types.h"

namespace UiFormat {

//  SWR: 2 decimals below 2.0, 1 decimal to 10.0, integer beyond, clamped 9999.
void swr(char* out, size_t n, double s);

//  dBm from tenths-of-a-dBm, e.g. -125 -> "-12.5dBm".
void dbm(char* out, size_t n, int16_t db10m);

//  Power in mW, ONE decimal, in mW and W only: 0.5mW / 999.9mW / 1.5W / 100.5W
//  / 2000W.  No kW (a 2 kW ceiling reads "2000W") and no sub-mW range, so the
//  widest string it can produce is "999.9mW" - MeterScreen anchors its readouts
//  from that.  Takes no detector type: the ranging no longer depends on one.
void powerMw(char* out, size_t n, double pwr);

//  Split a full-scale value in mW into mantissa + unit string for a bar scale.
//  `range` needs room for 3 chars.
void scalePowerMeter(double fs, double* outFs, char* range);

//  One stored AD8307 calibration point, as the Calibrate and About screens show
//  it.  Forward and reverse carry their own reference level (see CalPoint in
//  Types.h), so this prints ONE level while they agree:
//
//      P1: +40.0 dBm  F=2.233 R=2.233
//
//  and BOTH once a reverse-only calibration has split them, because at that
//  point a single number would be a lie about one of the two fits:
//
//      P1: F+40.0 R+20.0  F=2.233 R=1.100
//
//  Needs 40 chars to never truncate.  Lives here rather than in either screen
//  because it is pure formatting - which also puts it under `pio test -e native`.
void calPoint(char* out, size_t n, const char* tag, const CalPoint& p);

//*********************************************************************************
//  AUTOSCALE
//
//  A sliding maximum over the last SAMPLES readings, quantised to the user's
//  three scale-range presets (11/22/55 by default) times a decade.
//
//  The hold time is SAMPLES * the caller's tick period - it is counted in TICKS,
//  not milliseconds.  At METER_TICK_MS = 10 that is 300 ms.  Feeding this at a
//  different rate silently changes how twitchy the meter feels, which is why it
//  is a class with explicit state rather than a function with a static buffer.
//*********************************************************************************
class AutoScale {
public:
  static constexpr uint16_t SAMPLES = 30;

  //  Push one reading (mW) and return the chosen full-scale value (mW).
  double push(double pMw, const Settings& s);

private:
  double   buf_[SAMPLES] = { 0 };
  uint16_t idx_ = 0;
};

}  // namespace UiFormat

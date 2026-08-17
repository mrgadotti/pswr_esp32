//*********************************************************************************
//  PowerMath.h  -  Detector volts in, power / PEP / peak / SWR out.
//
//  PORTABLE BY CONSTRUCTION.  No Arduino, no FreeRTOS, no I2C, no LVGL - only
//  <math.h> and the shared Types.h.  Feed it a pair of detector voltages and it
//  gives back a filled MeterReadings.  That makes it:
//    - reusable in any project with a different front-end or a different MCU,
//    - unit-testable on the host with `pio test -e native`, which matters
//      because this is where the physics lives.
//
//  It is also REENTRANT, unlike the original.  The legacy version kept its
//  windows in function-local statics, so a second instance would have silently
//  shared them; here they are members, and two meters can run side by side.
//
//  CADENCE IS THE CALLER'S JOB, and it is load-bearing.  Every window below is
//  counted in SAMPLES, not milliseconds:
//      addSample()  once per acquisition. BUF_SHORT samples = the "100 ms" peak
//                   window, AVG_BUF1S samples = the "1 s" average.  Those names
//                   only tell the truth at SAMPLE_MS = 2.
//      updateSwr()  slower, at the UI poll rate; AVG_BUFSWR calls = the SWR
//                   average window.
//  Call them at other rates and the meter still works, but "100 ms" and "1 s"
//  quietly stop meaning 100 ms and 1 s.
//
//  All algorithms are a faithful port of PSWRsamplerBuf.ino - the stored
//  calibration of a real meter depends on them, so treat changes here as
//  changes to measurement accuracy, not to code style.
//*********************************************************************************
#pragma once

#include <stdint.h>

#include "AppConfig.h"
#include "Types.h"

class PowerMath {
public:
  //  Clear every window.  Call before the first sample so the averages fill
  //  from empty rather than from whatever was in memory.
  void reset();

  //  One acquisition: converts volts to instantaneous power, then advances the
  //  peak, PEP and averaging windows.  `out` carries state between calls (the
  //  reverse flag, fInst/rInst and the sticky SWR alarm), so pass the same
  //  MeterReadings every time.
  void addSample(float fwdVolts, float revVolts, const Settings& s, MeterReadings& out);

  //  Recompute SWR, its running average, peak/PEP in mW, and the
  //  power-detected / alarm flags from the figures addSample() produced.
  void updateSwr(const Settings& s, MeterReadings& out);

private:
  void determineDbm(const Settings& s, MeterReadings& out);

  //  Peak-hold over BUF_SHORT samples, in centi-dB to keep the scan integer.
  int32_t  peakBuf_[BUF_SHORT] = { 0 };
  uint16_t peakIdx_ = 0;

  //  PEP hold, advanced once per completed peak window.  Sized for the largest
  //  selectable window; the active one is PEP_OPTIONS[settings.pepIdx].
  int32_t  pepBuf_[PEP_MAX_WINDOW] = { 0 };
  uint16_t pepIdx_ = 0;

  //  Which window pepBuf_ was last filled under.  The operator can change it
  //  mid-transmission from the front panel, and the buffer has to be dropped
  //  when they do - see addSample().  0 is "not armed yet", so the first sample
  //  after a reset() always re-arms.
  uint16_t pepWin_ = 0;

  //  Two sliding-sum averages.  These two arrays are the single largest RAM
  //  cost in the measurement chain - see the memory notes in AppConfig.h.
  AvgSample avgBuf_[AVG_BUFSHORT] = { 0 };
  AvgSample avg1sBuf_[AVG_BUF1S]  = { 0 };
  AvgSample avgAccum_   = 0;
  AvgSample avg1sAccum_ = 0;
  uint16_t  avgIdx_     = 0;
  uint16_t  avg1sIdx_   = 0;

  AvgSample swrBuf_[AVG_BUFSWR] = { 0 };
  AvgSample swrAccum_ = 0;
  uint16_t  swrIdx_   = 0;
};

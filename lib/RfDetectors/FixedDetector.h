//*********************************************************************************
//  FixedDetector.h  -  RfDetector that always reads a fixed, non-simulated
//  value.  Two uses:
//
//    - THE STARTING POINT FOR A NEW ADC.  Wiring in a chip usually goes
//      "does it answer on the bus" -> "can the rest of the meter run with
//      something in place of real readings" -> "read the real registers" -
//      this class is that middle step. Copy it, rename it, and replace
//      read() with real hardware access once that part is ready; everything
//      downstream (PowerMath, the UI) already works against RfDetector and
//      does not care which step you are on.
//    - A deterministic bring-up build with no ADC wired at all.  Unlike
//      MockDetector's random walk (built to exercise autoscale and the UI's
//      worst-case redraw cost), this holds still, which is what you want when
//      checking one specific PowerMath or UI number by hand.
//
//  present() returns true on purpose: this class stands in for real hardware
//  that is not finished yet, not for "no ADC fitted" - that is MockDetector's
//  job, and it must not be duplicated here. See MeterEngine.cpp for how
//  ADC_FIXED_VALUES selects this over Ads1015Detector at compile time.
//*********************************************************************************
#pragma once

#include <stdint.h>

#include "RfDetector.h"

class FixedDetector : public RfDetector {
public:
  //  One "coupler", always fitted.  Same shape as Ads1015Detector::probeAll()
  //  so MeterEngine.cpp's call site does not change when this is swapped in -
  //  sda/scl/hz are unused here, kept only for that symmetry.
  static constexpr uint8_t ADDR_N = 1;
  static uint8_t probeAll(uint8_t sda, uint8_t scl, uint32_t hz,
                           RfDetector* fitted[ADDR_N]);

  bool    begin() override { return true; }
  bool    present() const override { return true; }
  void    read(float& fwdVolts, float& revVolts) override;
  uint8_t diagId() const override { return 0; }
};

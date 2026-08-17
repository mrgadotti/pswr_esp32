//*********************************************************************************
//  MockDetector.h  -  Simulated detector, used when no ADS1015 answers.
//
//  Synthesises the detector VOLTAGES that the normal math turns back into a
//  chosen power and SWR - it does not shortcut the math.  Two consequences worth
//  knowing: the whole measurement chain is exercised exactly as it would be with
//  real RF, and the simulation stays consistent with the meter's stored
//  calibration instead of drifting away from it after a recalibration.
//
//  The signal is a RANDOM WALK, not a constant.  A frozen mock leaves every
//  widget holding still, which hides what the UI actually costs: measured on
//  this board, a still screen ran at ~285 us per frame and a moving one at
//  ~4,400 us.  A UI that looks free on the bench and then struggles on real RF
//  is the worst outcome, so the mock moves.
//
//  The walk is multiplicative - a step in dB, not in watts - with mean reversion
//  toward the profile's centre, so it exercises the autoscale the way a real
//  signal does instead of jittering inside one range.  Step and reversion are
//  tuned together in AppConfig.h: the excursion settles around
//  step / sqrt(2 * reversion), so both must shrink to slow the walk without
//  also shrinking its range.
//
//  ONE INSTANCE PER SIMULATED COUPLER, each with its own MockProfile, because
//  the interesting thing about a second coupler is that it reads differently.
//  Only the selected one is ever sampled, so the others simply stop walking -
//  which is right: a coupler with no RF through it has nothing to report.
//*********************************************************************************
#pragma once

#include "AppConfig.h"
#include "RfDetector.h"
#include "Types.h"

class MockDetector : public RfDetector {
public:
  //  Needs the live Settings because it inverts the SAME calibration the math
  //  will then apply - that is what keeps the simulated reading honest.  The
  //  profile is what makes CPL1 a transceiver and CPL2 an amplifier.
  MockDetector(const Settings& settings, const MockProfile& profile)
    : s_(&settings), p_(&profile) {}

  bool begin() override;
  bool present() const override { return false; }   // never claims to be real
  void read(float& fwdVolts, float& revVolts) override;

private:
  const Settings*    s_ = nullptr;
  const MockProfile* p_ = nullptr;

  double logWatts_ = 0;      // walk state, in log10(watts)
  double swr_      = 0;
  double swrStep_  = 0;      // derived from the profile's band, see begin()
};

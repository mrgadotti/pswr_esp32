//*********************************************************************************
//  FixedDetector.cpp
//*********************************************************************************
#include "FixedDetector.h"

//  Mid-scale on the AD8307 two-point default fit (CALFWD1_DEFAULT /
//  CALREV1_DEFAULT in AppConfig.h are both 2.233 V at 40 dBm) - change these
//  to whatever is useful while the real driver for your ADC is not ready yet.
static constexpr float FWD_VOLTS = 1.000f;
static constexpr float REV_VOLTS = 0.200f;

uint8_t FixedDetector::probeAll(uint8_t /*sda*/, uint8_t /*scl*/, uint32_t /*hz*/,
                                 RfDetector* fitted[ADDR_N])
{
  static FixedDetector det;
  fitted[0] = &det;
  return 1;
}

void FixedDetector::read(float& fwdVolts, float& revVolts)
{
  fwdVolts = FWD_VOLTS;
  revVolts = REV_VOLTS;
}

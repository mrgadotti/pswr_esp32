//*********************************************************************************
//  Ads1015Detector.h  -  ADS1015 12-bit I2C ADC as an RfDetector.
//
//  Raw register access rather than the Adafruit_ADS1X15 library: the whole
//  driver is three transactions and this way the conversion timing is explicit
//  and there is no dependency to keep in step.  Register and method names below
//  follow Adafruit_ADS1X15's naming (readADC_SingleEnded, computeVolts,
//  REG_POINTER_*, REG_CONFIG_*) on purpose - anyone who has used that library
//  recognises this one immediately, only the plumbing underneath differs.  Swap
//  to another ADC by writing a new RfDetector, same as MockDetector does; this
//  class is the only thing in the project that knows an ADS1015 exists (see
//  MULTIPLE DEVICES below and MeterEngine.cpp).
//
//  Config: single-shot, PGA +/-4.096 V (LSB = 2 mV), 3300 SPS.  AIN0 = forward,
//  AIN1 = reflected - a property of how the coupler is wired to the converter,
//  identical on every board this firmware builds for.
//
//  PGA is +/-4.096 V on purpose, even though the board is 3V3 and the range can
//  never be filled.  The next step down (+/-2.048 V) would halve the LSB to 1 mV
//  and double the dB resolution - but CALFWD1_DEFAULT is already 2.233 V at
//  40 dBm and CAL_DBM_MAX allows 53, i.e. ~2.54 V, so it would clip the top of
//  the range.  Do not "optimise" it without re-reading those two constants.
//
//  TIMING.  A single-shot conversion at 3300 SPS takes ~303 us, so read() costs
//  two conversions plus four I2C transactions - roughly 1.0 ms of the 2 ms
//  sample budget at 400 kHz, most of it spent in delayMicroseconds().  That is
//  why the measurement task sits on core 0 by itself: it is mostly waiting, and
//  it must not be sharing a core with anything that could delay it.
//
//  MULTIPLE DEVICES.  The ADS1015 takes one of four addresses depending on how
//  its ADDR pin is strapped, so up to four can share the bus - which is how this
//  meter supports more than one coupler: one converter per coupler, each with
//  its own pair of detectors and therefore its own calibration.  probeAll()
//  does the construct-one-per-address-and-see-who-answers dance in one call, so
//  MeterEngine.cpp names this class exactly once (see MeterEngine.cpp) - that
//  one call is what a different ADC's equivalent replaces.
//*********************************************************************************
#pragma once

#include <stdint.h>

#include "RfDetector.h"

class Ads1015Detector : public RfDetector {
public:
  //  Every address an ADS1015 can be strapped to, ascending.  Coupler N is the
  //  Nth of these that answers, so the numbering follows the wiring rather than
  //  the probe order.
  static constexpr uint8_t ADDR_N = 4;
  static const uint8_t     ADDRESSES[ADDR_N];

  //  Bring the I2C bus up.  Idempotent: only the first call touches the
  //  peripheral, so every detector on the bus may call it without the driver
  //  complaining about a re-init.
  static void busBegin(uint8_t sda, uint8_t scl, uint32_t hz);

  //  Construct one instance per possible address, probe each, and report which
  //  answered.  Owns the instances (static storage internally) - fitted[] gets
  //  RfDetector pointers to them, in address order, so the caller never needs
  //  this class's own type beyond this one call.  fitted[] must have room for
  //  ADDR_N entries.  Returns the number filled - MeterEngine's coupler count
  //  when this is the ADC in use.
  static uint8_t probeAll(uint8_t sda, uint8_t scl, uint32_t hz,
                           RfDetector* fitted[ADDR_N]);

  //  sda/scl/address come from the SELECTED board's I2C pins in Pins.h; pass
  //  others to reuse this outside the meter entirely.
  Ads1015Detector(uint8_t sda, uint8_t scl, uint8_t address, uint32_t hz = 400000)
    : sda_(sda), scl_(scl), addr_(address), hz_(hz) {}

  bool    begin() override;
  bool    present() const override { return present_; }
  void    read(float& fwdVolts, float& revVolts) override;
  uint8_t diagId() const override { return addr_; }   // the I2C address

private:
  void     writeRegister(uint8_t reg, uint16_t value);
  uint16_t readRegister(uint8_t reg);
  int16_t  readADC_SingleEnded(uint8_t channel);   // channel: 0 = AIN0, 1 = AIN1
  float    computeVolts(int16_t counts);

  uint8_t  sda_, scl_, addr_;
  uint32_t hz_;
  bool     present_ = false;
};

//*********************************************************************************
//  Ads1015Detector.cpp
//*********************************************************************************
#include "Ads1015Detector.h"

#include <Arduino.h>
#include <Wire.h>

//  Register pointers and config bits, named after Adafruit_ADS1X15's own
//  ADS1X15_REG_* constants (same values) so the config word below reads the
//  same way it would against that library's datasheet-derived names.
static constexpr uint8_t  REG_POINTER_CONVERT = 0x00;
static constexpr uint8_t  REG_POINTER_CONFIG  = 0x01;

static constexpr uint16_t REG_CONFIG_OS_SINGLE    = 0x8000;  // start a conversion
static constexpr uint16_t REG_CONFIG_MUX_SINGLE_0 = 0x4000;  // AIN0 vs GND
static constexpr uint16_t REG_CONFIG_MUX_SINGLE_1 = 0x5000;  // AIN1 vs GND
static constexpr uint16_t REG_CONFIG_PGA_4_096V   = 0x0200;  // see header: do not "optimise"
static constexpr uint16_t REG_CONFIG_MODE_SINGLE  = 0x0100;
//  DR2:DR0 at bits 7:5 -> 0x00C0 = 0b110 = 3300 SPS on the ADS1015.  CONV_US
//  below is derived from this value and nothing checks the two agree, so if
//  this ever changes, CONV_US must change with it.
static constexpr uint16_t REG_CONFIG_DR_3300SPS   = 0x00C0;
static constexpr uint16_t REG_CONFIG_CQUE_NONE    = 0x0003;  // comparator off

static constexpr uint16_t CFG_BASE = (REG_CONFIG_OS_SINGLE  | REG_CONFIG_PGA_4_096V |
                                       REG_CONFIG_MODE_SINGLE | REG_CONFIG_DR_3300SPS |
                                       REG_CONFIG_CQUE_NONE);

static constexpr uint8_t  BIT_SHIFT = 4;        // 12-bit result, left-aligned in 16
static constexpr float    LSB_VOLTS = 0.002f;   // 4.096 V / 2048 counts
static constexpr uint32_t CONV_US   = 350;      // 1/3300 s = 303 us, +15% margin

//  ADDR pin strapping: GND, VDD, SDA, SCL.
const uint8_t Ads1015Detector::ADDRESSES[Ads1015Detector::ADDR_N] = {
  0x48, 0x49, 0x4A, 0x4B
};

//  One flag rather than asking the driver: Wire.begin() on an already-running
//  bus is harmless but logs a warning per call, and with four detectors probing
//  the same bus that is three lines of noise at every boot.
static bool gBusReady = false;

void Ads1015Detector::busBegin(uint8_t sda, uint8_t scl, uint32_t hz)
{
  if (gBusReady) return;
  Wire.begin(sda, scl, hz);
  gBusReady = true;
}

bool Ads1015Detector::begin()
{
  busBegin(sda_, scl_, hz_);

  //  Address-only probe: an ACK is all that distinguishes "fitted" from "not".
  Wire.beginTransmission(addr_);
  present_ = (Wire.endTransmission() == 0);
  return present_;
}

uint8_t Ads1015Detector::probeAll(uint8_t sda, uint8_t scl, uint32_t hz,
                                   RfDetector* fitted[ADDR_N])
{
  //  Static: these back the RfDetector pointers handed out below for the life
  //  of the program, and there is only ever one bus of them.
  static Ads1015Detector dets[ADDR_N] = {
    { sda, scl, ADDRESSES[0], hz },
    { sda, scl, ADDRESSES[1], hz },
    { sda, scl, ADDRESSES[2], hz },
    { sda, scl, ADDRESSES[3], hz },
  };

  uint8_t count = 0;
  for (uint8_t i = 0; i < ADDR_N; i++)
    if (dets[i].begin()) fitted[count++] = &dets[i];

  return count;
}

void Ads1015Detector::writeRegister(uint8_t reg, uint16_t value)
{
  Wire.beginTransmission(addr_);
  Wire.write(reg);
  Wire.write((uint8_t)(value >> 8));
  Wire.write((uint8_t)(value & 0xFF));
  Wire.endTransmission();
}

uint16_t Ads1015Detector::readRegister(uint8_t reg)
{
  Wire.beginTransmission(addr_);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return 0;

  Wire.requestFrom(addr_, (uint8_t)2);
  if (Wire.available() < 2) return 0;

  return (Wire.read() << 8) | Wire.read();
}

int16_t Ads1015Detector::readADC_SingleEnded(uint8_t channel)
{
  uint16_t mux = (channel == 0) ? REG_CONFIG_MUX_SINGLE_0 : REG_CONFIG_MUX_SINGLE_1;

  writeRegister(REG_POINTER_CONFIG, CFG_BASE | mux);
  delayMicroseconds(CONV_US);

  int16_t raw = (int16_t)readRegister(REG_POINTER_CONVERT) >> BIT_SHIFT;
  return raw < 0 ? 0 : raw;        // detector output is single-ended positive
}

float Ads1015Detector::computeVolts(int16_t counts)
{
  return counts * LSB_VOLTS;
}

void Ads1015Detector::read(float& fwdVolts, float& revVolts)
{
  fwdVolts = computeVolts(readADC_SingleEnded(0));
  revVolts = computeVolts(readADC_SingleEnded(1));
}

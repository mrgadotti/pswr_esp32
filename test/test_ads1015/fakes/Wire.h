//*********************************************************************************
//  Wire.h  -  A fake I2C bus standing in for an ADS1015, so
//  Ads1015Detector.cpp - the real driver, unmodified - can be host-compiled and
//  exercised against a simulated chip instead of hardware.
//
//  Models exactly the two transactions the driver performs, per address:
//    - a "set pointer register" write (1 byte: the register), or a full
//      register write (3 bytes: register + big-endian value) - both land in
//      pointerReg, the write additionally lands in configReg;
//    - a 2-byte read, which the driver only ever issues right after pointing
//      at REG_POINTER_CONVERT, so that is the only register requestFrom()
//      needs to answer.  The channel comes back out of the MUX bits the driver
//      itself just wrote to configReg - that is what makes AIN0 and AIN1
//      answer with the two different values a test sets up.
//
//  A test drives this by writing directly into chips[address].present and
//  .conv[0]/.conv[1] - the raw, left-aligned 12-in-16-bit register content a
//  real ADS1015 would return, i.e. exactly what Ads1015Detector.cpp's
//  `(int16_t)readRegister(...) >> BIT_SHIFT` expects to shift down.
//*********************************************************************************
#pragma once

#include <stdint.h>

class TwoWire {
public:
  static constexpr unsigned ADDR_SPACE = 0x80;   // 7-bit I2C address range

  struct Chip {
    bool    present = false;
    int16_t conv[2] = { 0, 0 };   // raw conversion register, AIN0 / AIN1

    uint16_t pointerReg = 0;
    uint16_t configReg  = 0;
  };

  Chip chips[ADDR_SPACE];

  void begin(int /*sda*/, int /*scl*/, uint32_t /*hz*/) {}

  void beginTransmission(uint8_t addr)
  {
    curAddr_ = addr;
    txLen_   = 0;
  }

  size_t write(uint8_t b)
  {
    if (txLen_ < sizeof(txBuf_)) txBuf_[txLen_++] = b;
    return 1;
  }

  uint8_t endTransmission()
  {
    Chip& c = chips[curAddr_];
    if (!c.present) return 2;   // NACK on address, like an absent chip

    if (txLen_ >= 1) {
      c.pointerReg = txBuf_[0];
      if (txLen_ == 3 && c.pointerReg == 0x01 /* REG_POINTER_CONFIG */)
        c.configReg = (uint16_t(txBuf_[1]) << 8) | txBuf_[2];
    }
    return 0;
  }

  uint8_t requestFrom(uint8_t addr, uint8_t len)
  {
    rxLen_ = 0;
    rxPos_ = 0;

    Chip& c = chips[addr];
    if (!c.present || c.pointerReg != 0x00 /* REG_POINTER_CONVERT */) return 0;

    //  MUX_SINGLE_0 = 0x4000, MUX_SINGLE_1 = 0x5000 - bit 12 is the only one
    //  that tells them apart, so it survives being OR'd with OS_SINGLE and the
    //  PGA/MODE/DR/CQUE bits (none of which touch bit 12) into the real config
    //  word the driver writes.  A wider mask here would silently stop matching
    //  the moment CFG_BASE's own bits are folded in - which is exactly what
    //  happened the first time this was written against 0x4000/0x5000 alone.
    const int      channel = (c.configReg & 0x1000) ? 1 : 0;
    const uint16_t value   = (uint16_t)c.conv[channel];

    rxBuf_[rxLen_++] = (uint8_t)(value >> 8);
    rxBuf_[rxLen_++] = (uint8_t)(value & 0xFF);
    return (len < rxLen_) ? len : rxLen_;
  }

  int available() const { return (int)rxLen_ - (int)rxPos_; }
  int read() { return (rxPos_ < rxLen_) ? rxBuf_[rxPos_++] : -1; }

private:
  uint8_t curAddr_ = 0;
  uint8_t txBuf_[3] = { 0, 0, 0 };
  uint8_t txLen_    = 0;
  uint8_t rxBuf_[2] = { 0, 0 };
  uint8_t rxLen_    = 0;
  uint8_t rxPos_    = 0;
};

inline TwoWire Wire;

//*********************************************************************************
//  Ili9341Driver.h  -  Display + touch hardware.
//
//  Owns the TFT_eSPI panel and the touch controller: XPT2046 (on its own SPI
//  host, never the panel's) on two boards, FT6336 (on I2C) on the third - which
//  one is picked by BOARD_TOUCH_IS_I2C, see Pins.h.  Pure hardware abstraction -
//  NO business logic, and no LVGL: the binding to LVGL lives in LvglPort, which
//  is the only place that includes both.  The TFT object is exposed by reference
//  so LvglPort can flush to it.
//
//  BOARD-INDEPENDENT BY CONSTRUCTION.  Every pin, bus, rotation and quirk comes
//  from Pins.h (which resolves to one of include/boards/*.h) or from TFT_eSPI's
//  own build flags, so supporting a second panel module cost this file nothing
//  but the BOARD_* names below.  Anything that reads like "the CYD does X" and
//  is written as a literal here is a bug waiting for the next board.
//*********************************************************************************
#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>

#include "Backlight.h"
#include "Pins.h"
#include "RawTouch.h"

#if BOARD_TOUCH_IS_I2C
  #include <FT6336.h>
#else
  #include <XPT2046_Touchscreen.h>
#endif

//  Panel geometry after rotation, from the selected board profile.  Kept as
//  constexpr ints rather than used as raw macros because half the UI does signed
//  arithmetic with them (SCREEN_W - 1, centre offsets), and a bare macro would
//  drag whatever type the literal happened to have into every one of those.
constexpr int16_t SCREEN_W = BOARD_SCREEN_W;
constexpr int16_t SCREEN_H = BOARD_SCREEN_H;

class Ili9341Driver : public RawTouch, public Backlight {
public:
  Ili9341Driver();

  //  Initialise the panel (rotation/inversion) and the touch controller.
  void begin();

  //  Hand the SPI host to the IDF spi_master driver so pushPixelsDMA() works.
  //  Call once, after begin(), before any DMA push.  Returns false on failure.
  bool beginDma();

  //  Exposed drawing surface (LvglPort's flush target).
  TFT_eSPI& tft() { return tft_; }

  //  Touch.  readTouch() returns true and fills mapped screen coordinates when
  //  a press above the pressure threshold is detected.
  bool readTouch(int& x, int& y);
  bool touched();

  //  RawTouch: unmapped counts and a runtime remap, for the calibration screen.
  bool readRaw(int& x, int& y) override;
  void setTouchCal(int16_t minX, int16_t maxX, int16_t minY, int16_t maxY) override;

  //  Backlight: PWM duty in percent.  On a board with BOARD_HAS_BACKLIGHT_PWM,
  //  begin() takes TFT_BL away from TFT_eSPI's plain on/off control and gives it
  //  to LEDC; on one without, this is accepted and ignored.
  void setBrightness(uint8_t percent) override;

private:
  //  Live mapping.  Seeded from Pins.h and overwritten from NVS at boot, so a
  //  calibration survives a power cycle.
  int16_t calMinX_ = TOUCH_MIN_X;
  int16_t calMaxX_ = TOUCH_MAX_X;
  int16_t calMinY_ = TOUCH_MIN_Y;
  int16_t calMaxY_ = TOUCH_MAX_Y;

  TFT_eSPI tft_;

#if BOARD_TOUCH_IS_I2C
  //  Constructed with the FT6336's native (pre-rotation) resolution; begin()
  //  supplies the wiring and setRotation() the BOARD_TOUCH_ROTATION, both in
  //  Ili9341Driver.cpp.  Vendored verbatim in lib/FT6336 - see the board
  //  header for why a hand-rolled reader was replaced with it.
  FT6336 touch_;
#else
  SPIClass            touchSpi_;
  XPT2046_Touchscreen touch_;
#endif
};

//*********************************************************************************
//  Ili9341Driver.h  -  Display + touch hardware for the CYD.
//
//  Owns the TFT_eSPI panel and the XPT2046 touch controller (on its own VSPI
//  bus).  Pure hardware abstraction - NO business logic, and no LVGL: the
//  binding to LVGL lives in LvglPort, which is the only place that includes
//  both.  The TFT object is exposed by reference so LvglPort can flush to it.
//*********************************************************************************
#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include "Backlight.h"
#include "Pins.h"
#include "RawTouch.h"

//  Panel geometry (landscape, rotation 1).
constexpr int16_t SCREEN_W = 320;
constexpr int16_t SCREEN_H = 240;

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

  //  Backlight: PWM duty in percent.  begin() takes TFT_BL away from TFT_eSPI's
  //  plain on/off control and gives it to LEDC.
  void setBrightness(uint8_t percent) override;

private:
  //  Live mapping.  Seeded from Pins.h and overwritten from NVS at boot, so a
  //  calibration survives a power cycle.
  int16_t calMinX_ = TOUCH_MIN_X;
  int16_t calMaxX_ = TOUCH_MAX_X;
  int16_t calMinY_ = TOUCH_MIN_Y;
  int16_t calMaxY_ = TOUCH_MAX_Y;

  TFT_eSPI            tft_;
  SPIClass            touchSpi_;
  XPT2046_Touchscreen touch_;
};

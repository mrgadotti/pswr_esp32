//*********************************************************************************
//  Ili9341Driver.cpp
//*********************************************************************************
#include "Ili9341Driver.h"

#include "AppConfig.h"
#include "Pins.h"

//  The touch controller lives on its own SPI bus - the panel has HSPI to itself
//  (USE_HSPI_PORT), which is what makes DMA flushing safe alongside touch reads.
Ili9341Driver::Ili9341Driver()
  : tft_(),
    touchSpi_(VSPI),
    touch_(TOUCH_CS, TOUCH_IRQ)
{
}

void Ili9341Driver::begin()
{
  tft_.init();
  tft_.invertDisplay(true);
  tft_.setRotation(1);
  tft_.fillScreen(TFT_BLACK);

  touchSpi_.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch_.begin(touchSpi_);
  touch_.setRotation(1);

  //  AFTER tft_.init(), which does its own pinMode/digitalWrite on TFT_BL.
  //  Attaching LEDC to the pin overrides that, so the order matters: doing it
  //  first would leave the panel back at plain on/off.
  ledcSetup(BACKLIGHT_PWM_CH, BACKLIGHT_PWM_HZ, BACKLIGHT_PWM_BITS);
  ledcAttachPin(TFT_BL, BACKLIGHT_PWM_CH);
  setBrightness(BRIGHTNESS_DEFAULT);
}

void Ili9341Driver::setBrightness(uint8_t percent)
{
  if (percent < BRIGHTNESS_MIN) percent = BRIGHTNESS_MIN;
  if (percent > BRIGHTNESS_MAX) percent = BRIGHTNESS_MAX;

  //  TFT_BACKLIGHT_ON says which level lights the panel; on the CYD it is HIGH,
  //  but honouring it here keeps this working on a board wired the other way.
  const uint32_t full = (1u << BACKLIGHT_PWM_BITS) - 1;
  uint32_t duty = (full * percent) / 100;
#if TFT_BACKLIGHT_ON == LOW
  duty = full - duty;
#endif
  ledcWrite(BACKLIGHT_PWM_CH, duty);
}

bool Ili9341Driver::readRaw(int& x, int& y)
{
  if (!touch_.tirqTouched() || !touch_.touched()) return false;
  TS_Point p = touch_.getPoint();
  if (p.z < TOUCH_MIN_PRESSURE) return false;
  x = p.x;
  y = p.y;
  return true;
}

void Ili9341Driver::setTouchCal(int16_t minX, int16_t maxX, int16_t minY, int16_t maxY)
{
  //  Reject a degenerate span rather than divide by zero in map() later.
  if (maxX - minX < 100 || maxY - minY < 100) return;
  calMinX_ = minX; calMaxX_ = maxX;
  calMinY_ = minY; calMaxY_ = maxY;
}

bool Ili9341Driver::readTouch(int& x, int& y)
{
  if (!readRaw(x, y)) return false;
  x = map(x, calMinX_, calMaxX_, 0, SCREEN_W);
  y = map(y, calMinY_, calMaxY_, 0, SCREEN_H);
  //  Clamp to the last valid pixel, not one past it.  Harmless with 76 px
  //  buttons, but LVGL hit-tests exactly: x == SCREEN_W would hit nothing.
  x = constrain(x, 0, SCREEN_W - 1);
  y = constrain(y, 0, SCREEN_H - 1);
  return true;
}

bool Ili9341Driver::touched() { return touch_.touched(); }

//  ctrl_cs = false: TFT_eSPI keeps driving CS itself, which is what
//  setAddrWindow() assumes.  Only meaningful once the panel has HSPI to itself.
bool Ili9341Driver::beginDma() { return tft_.initDMA(false); }

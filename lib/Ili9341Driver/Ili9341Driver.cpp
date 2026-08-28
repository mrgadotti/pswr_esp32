//*********************************************************************************
//  Ili9341Driver.cpp
//*********************************************************************************
#include "Ili9341Driver.h"

#include "AppConfig.h"
#include "Pins.h"

#if BOARD_TOUCH_IS_I2C
//  FT6336 is on I2C, so there is no host to contend with the panel's SPI for.
//  The constructor only needs the controller's native (pre-rotation) panel
//  resolution - the wiring and BOARD_TOUCH_ROTATION are supplied in begin(),
//  matching how the XPT2046 branch below defers its own setup the same way.
Ili9341Driver::Ili9341Driver()
  : tft_(),
    touch_(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, TOUCH_NATIVE_W, TOUCH_NATIVE_H)
{
}
#else
//  The touch controller lives on its own SPI host - the panel has the other one
//  to itself, which is what makes DMA flushing safe alongside touch reads.
//  Which host is which is a board property (BOARD_TOUCH_SPI_BUS, checked against
//  the panel's USE_HSPI_PORT in Pins.h): VSPI here on the CYD, HSPI on the
//  discrete ESP32 + ILI9341 build, where the panel takes VSPI's IO_MUX pins.
Ili9341Driver::Ili9341Driver()
  : tft_(),
    touchSpi_(BOARD_TOUCH_SPI_BUS),
    touch_(TOUCH_CS, TOUCH_IRQ)
{
}
#endif

void Ili9341Driver::begin()
{
  tft_.init();

  //  Only the CYD's panel is fitted colour-inverted.  Calling this
  //  unconditionally on a stock module gives a display that works perfectly, in
  //  negative - which looks like a broken theme, not a one-flag board setting.
#if BOARD_TFT_INVERT
  tft_.invertDisplay(true);
#endif

  tft_.setRotation(BOARD_TFT_ROTATION);
  tft_.fillScreen(TFT_BLACK);

#if BOARD_TOUCH_IS_I2C
  //  FT6336::begin() owns Wire.begin(), the RESET pulse (with the ~500 ms
  //  settle the chip needs before it will ACK on I2C) and a chip-ID sanity
  //  check - all internal to lib/FT6336, none of it repeated here.
  touch_.begin();
  touch_.setRotation(BOARD_TOUCH_ROTATION);
#else
  touchSpi_.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch_.begin(touchSpi_);
  touch_.setRotation(BOARD_TOUCH_ROTATION);
#endif

#if BOARD_HAS_BACKLIGHT_PWM
  //  AFTER tft_.init(), which does its own pinMode/digitalWrite on TFT_BL.
  //  Attaching LEDC to the pin overrides that, so the order matters: doing it
  //  first would leave the panel back at plain on/off.
  ledcSetup(BACKLIGHT_PWM_CH, BACKLIGHT_PWM_HZ, BACKLIGHT_PWM_BITS);
  ledcAttachPin(TFT_BL, BACKLIGHT_PWM_CH);
  setBrightness(BRIGHTNESS_DEFAULT);
#endif
}

void Ili9341Driver::setBrightness(uint8_t percent)
{
#if BOARD_HAS_BACKLIGHT_PWM
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
#else
  //  A board whose LED pin is tied to 3V3 has a backlight, just not one this
  //  firmware can move.  Accepting and ignoring the setting beats making the
  //  menu conditional: the stored value stays valid, so wiring the pin later is
  //  a board-header flag and nothing else.
  (void)percent;
#endif
}

bool Ili9341Driver::readRaw(int& x, int& y)
{
#if BOARD_TOUCH_IS_I2C
  //  FT6336::read() already applies BOARD_TOUCH_ROTATION (set in begin()) to
  //  the raw controller point before returning it, so points[0] is already in
  //  BOARD_SCREEN_W x BOARD_SCREEN_H space - see the board header for why
  //  TOUCH_MIN/MAX is then a plain identity map over this.
  touch_.read();
  if (!touch_.isTouched) return false;
  x = touch_.points[0].x;
  y = touch_.points[0].y;
  return true;
#else
  if (!touch_.tirqTouched() || !touch_.touched()) return false;
  TS_Point p = touch_.getPoint();
  if (p.z < TOUCH_MIN_PRESSURE) return false;
  x = p.x;
  y = p.y;
  return true;
#endif
}

void Ili9341Driver::setTouchCal(int16_t minX, int16_t maxX, int16_t minY, int16_t maxY)
{
  //  Reject a degenerate span rather than divide by zero in map() later.
  //
  //  The MAGNITUDE is what matters, not the sign.  A panel whose resistive layer
  //  is wired the other way round calibrates to min > max, and map() handles a
  //  reversed input range perfectly well - so testing `maxX - minX < 100` would
  //  have quietly refused every calibration on such a panel and left it stuck on
  //  the compile-time default forever.  Not hypothetical: it is the first thing
  //  that bites when the same firmware meets a different touch module.
  const int32_t spanX = (int32_t)maxX - (int32_t)minX;
  const int32_t spanY = (int32_t)maxY - (int32_t)minY;
  if (spanX > -100 && spanX < 100) return;
  if (spanY > -100 && spanY < 100) return;

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

bool Ili9341Driver::touched()
{
#if BOARD_TOUCH_IS_I2C
  touch_.read();
  return touch_.isTouched;
#else
  return touch_.touched();
#endif
}

//  ctrl_cs = false: TFT_eSPI keeps driving CS itself, which is what
//  setAddrWindow() assumes.  Only meaningful once the panel has a host to
//  itself, which every board profile guarantees.
bool Ili9341Driver::beginDma() { return tft_.initDMA(false); }

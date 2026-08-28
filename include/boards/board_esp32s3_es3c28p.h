//*********************************************************************************
//  board_esp32s3_es3c28p.h  -  2.8" IPS ESP32-S3 (QD electronic ES3C28P / ES3N28P).
//
//  An integrated board like the CYD, but built around an ESP32-S3 instead of an
//  ESP32, with an IPS ILI9341V panel and an FT6336 CAPACITIVE touch controller on
//  I2C - not the XPT2046 resistive controller on its own SPI bus the other two
//  boards use.  That is the one place this board does not fit the two-board
//  contract as it stood: BOARD_TOUCH_IS_I2C (see Pins.h) is what lets it opt out
//  of the SPI-touch fields and into the I2C ones instead, and Ili9341Driver picks
//  the matching read path from that same flag.  Everything else - the panel, the
//  backlight, the ADS1015 bus, the screen layout - is unchanged from the other
//  two boards, which is what "minimal change" meant when this board was added.
//
//  Included ONLY through Pins.h, which is also what checks that every name
//  required of a board header is actually present.  Do not include it directly.
//
//  The ILI9341's own pins are NOT here: they are TFT_eSPI build flags in
//  platformio.ini ([env:esp32s3_es3c28p]).  See the header of Pins.h for why that
//  split exists and is not going away.
//*********************************************************************************
#pragma once

#include <stdint.h>

#define BOARD_NAME  "2.8\" IPS ESP32-S3 (ES3C28P/ES3N28P)"

//*********************************************************************************
//  Panel
//*********************************************************************************
//  Geometry AFTER rotation, which is what LVGL is told and what the whole UI is
//  laid out against.  Same 320x240 as the other two boards, so no screen moves.
#define BOARD_SCREEN_W        320
#define BOARD_SCREEN_H        240
#define BOARD_TFT_ROTATION    1      // landscape, USB-C to the left

//  Confirmed on hardware: this IPS ILI9341V panel comes up colour-inverted
//  with the stock ILI9341_DRIVER init sequence, same as the CYD's panel (see
//  board_cyd.h) - unlike the plain TN panel on board_esp32_ili9341.h, which
//  does not need this.  IPS ILI9341 clones commonly need it; do not assume
//  "stock driver" implies "not inverted" for the next IPS panel either.
#define BOARD_TFT_INVERT      1

//*********************************************************************************
//  Touch  -  FT6336 capacitive, on I2C, sharing the bus with the ES8311 audio
//  codec this firmware does not use (cyd_gotchas: "do not call Wire.begin()
//  twice with different pins" - it never is, there is only one pin pair here).
//
//  Driven through the vendored lib/FT6336 (copied verbatim from the kit's own
//  Arduino library, not written against the datasheet from scratch) rather
//  than a hand-rolled register read: an earlier hand-rolled version compiled
//  fine but never registered a touch on real hardware, most likely the FT6336
//  needing a full ~500 ms settle after RESET before it will ACK on I2C, which
//  the vendor library's reset() waits out (and verifies via the chip's ID
//  registers) but a shorter ad hoc delay did not. TOUCH_NATIVE_W/H below are
//  the FT6336's constructor args - the panel's PORTRAIT native resolution,
//  which the library itself rotates into BOARD_TFT_ROTATION's LANDSCAPE frame
//  (see FT6336::setRotation() / readPoint() in lib/FT6336/FT6336.cpp) using
//  the SAME 0-3 numbering TFT_eSPI/XPT2046 already use in this project, so
//  Ili9341Driver passes BOARD_TOUCH_ROTATION straight through with no
//  translation table. Because that rotation is applied by the library before
//  Ili9341Driver ever sees a coordinate, TOUCH_MIN/MAX below is a plain
//  identity map over the ALREADY-ROTATED range - reusing the exact same
//  raw-to-screen mapping code, and the exact same Touch Cal screen, as the
//  other two boards. If touch tracks the wrong axis, try BOARD_TOUCH_ROTATION
//  = 3 (ROTATION_LEFT) instead of 1; if an axis is right but backwards, swap
//  that axis's MIN and MAX (Ili9341Driver::setTouchCal accepts a reversed
//  span) - same second knob the resistive boards document.
//*********************************************************************************
#define BOARD_TOUCH_IS_I2C    1
#define BOARD_TOUCH_ROTATION  1

constexpr uint8_t TOUCH_SDA = 16;
constexpr uint8_t TOUCH_SCL = 15;
constexpr uint8_t TOUCH_INT = 17;   // not used: this driver polls, it does not IRQ
constexpr uint8_t TOUCH_RST = 18;

//  FT6336's native (pre-rotation) panel resolution - portrait, the mirror of
//  BOARD_SCREEN_W/H above which are landscape (post-rotation).
constexpr uint16_t TOUCH_NATIVE_W = BOARD_SCREEN_H;   // 240
constexpr uint16_t TOUCH_NATIVE_H = BOARD_SCREEN_W;   // 320

//  Identity map: FT6336::readPoint() already rotates into SCREEN_W x
//  SCREEN_H pixel coordinates (see above), so this is the starting point AND,
//  unlike a resistive panel, is expected to stay exactly this on a healthy
//  board. Touch Cal still works if a specific unit needs nudging.
constexpr uint16_t TOUCH_MIN_X        = 0;
constexpr uint16_t TOUCH_MAX_X        = BOARD_SCREEN_W - 1;
constexpr uint16_t TOUCH_MIN_Y        = 0;
constexpr uint16_t TOUCH_MAX_Y        = BOARD_SCREEN_H - 1;

//  Unused by the I2C read path (FT6336 reports no analog pressure, only
//  touched/not-touched) - defined only because Pins.h's contract requires it
//  from every board, the same way LED_R/G/B are required even when
//  BOARD_HAS_RGB_LED is 0.
constexpr int16_t  TOUCH_MIN_PRESSURE = 1;

//*********************************************************************************
//  Backlight  -  TFT_BL (GPIO 45, set in platformio.ini) driven by LEDC.
//*********************************************************************************
#define BOARD_HAS_BACKLIGHT_PWM 1

//*********************************************************************************
//  Status LEDs  -  none in the sense this firmware means.  The kit has a single
//  addressable WS2812 (GPIO 42, cyd_pinout), not three individually-driven LEDs,
//  so it does not fit the LED_R/LED_G/LED_B contract below and is left
//  unfitted - the same supported configuration as the bare ESP32 devkit build.
//  Driving the WS2812 as a stand-in for the SWR/power indicators would be new
//  scope (a NeoPixel dependency, a colour policy), not a board port.
//*********************************************************************************
#define BOARD_HAS_RGB_LED     0
#define BOARD_LED_ACTIVE_LOW  0

constexpr uint8_t LED_R = 1;    // not fitted - see above; never pinMode'd
constexpr uint8_t LED_G = 2;
constexpr uint8_t LED_B = 3;

//*********************************************************************************
//  I2C bus for the ADS1015 converters.  Same physical bus as the FT6336 touch
//  controller above (SDA 16 / SCL 15) - I2C is multi-drop and the ADS1015's
//  0x48-0x4B addresses do not collide with the FT6336's 0x38, so sharing costs
//  nothing.  Ads1015Detector::busBegin() calls Wire.begin() again with these
//  same pins; that is a harmless no-op already relied on elsewhere in this
//  firmware (see the comment on Ads1015Detector::gBusReady), not a new risk.
//*********************************************************************************
constexpr uint8_t  I2C_SDA = TOUCH_SDA;
constexpr uint8_t  I2C_SCL = TOUCH_SCL;
constexpr uint32_t I2C_HZ  = 400000;

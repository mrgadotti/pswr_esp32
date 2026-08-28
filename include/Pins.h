//*********************************************************************************
//  Pins.h  -  Board selection.  Picks exactly ONE hardware profile out of
//             include/boards/ and puts its pin map in front of every module.
//
//  Supported targets, one -D each, set per environment in platformio.ini so that
//  changing board is a one-line edit and never a source edit:
//
//    BOARD_CYD              ESP32-2432S028R "Cheap Yellow Display"
//                             -> boards/board_cyd.h              [env:cyd]
//    BOARD_ESP32_ILI9341    ESP32 Dev Module + 2.8" ILI9341 + XPT2046 module
//                             -> boards/board_esp32_ili9341.h    [env:esp32dev_ili9341]
//    BOARD_ESP32S3_ES3C28P  2.8" IPS ESP32-S3 (ES3C28P / ES3N28P), FT6336
//                           capacitive touch on I2C instead of XPT2046 on SPI
//                             -> boards/board_esp32s3_es3c28p.h  [env:esp32s3_es3c28p]
//
//  WHAT LIVES WHERE, and why the hardware map is split across two files at all:
//
//    platformio.ini   the ILI9341's own pins (TFT_MISO/MOSI/SCLK/CS/DC/RST/BL),
//                     which SPI host the panel gets, the driver variant and the
//                     bus clock.  All of it belongs to TFT_eSPI, and TFT_eSPI is
//                     configured by MACROS at compile time under
//                     USER_SETUP_LOADED - it cannot read a constexpr out of this
//                     file, so moving it here would not be a simplification, it
//                     would be a second copy that silently disagrees.
//    boards/*.h       everything this firmware owns: the touch controller and
//                     its bus, the I2C bus, the status LEDs, the panel quirks
//                     and which optional peripherals the board actually has.
//
//  ADDING A FOURTH BOARD:  copy the closest board header, define every name in
//  the contract below, add an #elif here and an env in platformio.ini.  A name
//  left out is a compile error at the bottom of THIS file, next to the
//  documentation for what it means - not a mystery at runtime.
//*********************************************************************************
#pragma once

#include <stdint.h>

#if (defined(BOARD_CYD) + defined(BOARD_ESP32_ILI9341) + defined(BOARD_ESP32S3_ES3C28P)) > 1
  #error "Pins.h: two boards selected - define exactly one of BOARD_CYD / BOARD_ESP32_ILI9341 / BOARD_ESP32S3_ES3C28P"
#elif defined(BOARD_CYD)
  #include "boards/board_cyd.h"
#elif defined(BOARD_ESP32_ILI9341)
  #include "boards/board_esp32_ili9341.h"
#elif defined(BOARD_ESP32S3_ES3C28P)
  #include "boards/board_esp32s3_es3c28p.h"
#else
  #error "Pins.h: no board selected - build with -D BOARD_CYD, -D BOARD_ESP32_ILI9341 or -D BOARD_ESP32S3_ES3C28P (see platformio.ini)"
#endif

//*********************************************************************************
//  THE CONTRACT  -  what a board header owes the rest of the firmware.
//
//  Checked here rather than left to fail wherever the name happens to be used,
//  because "TOUCH_CS was not declared" in the middle of a driver is a much worse
//  clue than an error pointing at the list of things a board must provide.
//
//  Feature flags (macros, so they can drive #if):
//
//    BOARD_NAME               human-readable, for logs and the About screen
//    BOARD_SCREEN_W / _H      panel geometry AFTER rotation
//    BOARD_TFT_ROTATION       TFT_eSPI rotation, 0-3
//    BOARD_TFT_INVERT         1 if the panel needs invertDisplay(true)
//    BOARD_TOUCH_IS_I2C       0: XPT2046 resistive touch on its own SPI host
//                             1: FT6336 capacitive touch on I2C - see the two
//                                sub-contracts below, only one of which applies
//    BOARD_TOUCH_ROTATION     rotation applied to the raw touch reading, 0-3
//    BOARD_HAS_BACKLIGHT_PWM  1 if TFT_BL is a GPIO this firmware may drive
//    BOARD_HAS_RGB_LED        1 if status LEDs are fitted
//    BOARD_LED_ACTIVE_LOW     1 if a LOW lights them (only read when the above is 1)
//
//  Constants (constexpr, so they carry a type):
//
//    TOUCH_MIN_X/MAX_X/MIN_Y/MAX_Y          default raw-to-screen mapping
//    TOUCH_MIN_PRESSURE                     press threshold; on a
//                                            BOARD_TOUCH_IS_I2C board there is no
//                                            analog pressure, so this is unused
//                                            by the driver and only satisfies the
//                                            contract below
//    LED_R/LED_G/LED_B                      status LED pins (defined even when
//                                           BOARD_HAS_RGB_LED is 0, so that
//                                           fitting them later is one flag)
//    I2C_SDA/I2C_SCL/I2C_HZ                 ADS1015 bus
//
//  BOARD_TOUCH_IS_I2C == 0 additionally requires:
//    BOARD_TOUCH_SPI_BUS                    VSPI / HSPI - must NOT be the panel's host
//    TOUCH_CLK/MISO/MOSI/CS/IRQ             XPT2046 wiring
//
//  BOARD_TOUCH_IS_I2C == 1 additionally requires:
//    TOUCH_SDA/TOUCH_SCL/TOUCH_INT/TOUCH_RST     FT6336 wiring.  TOUCH_SDA/SCL
//                                                 may equal I2C_SDA/I2C_SCL if
//                                                 the board shares one bus
//                                                 between touch and the ADS1015
//                                                 - see the board header.
//    TOUCH_NATIVE_W/TOUCH_NATIVE_H                FT6336's native (pre-rotation)
//                                                 panel resolution - the lib/FT6336
//                                                 constructor args it rotates
//                                                 into BOARD_SCREEN_W/H itself.
//*********************************************************************************
#ifndef BOARD_NAME
  #error "board header must define BOARD_NAME"
#endif
#ifndef BOARD_SCREEN_W
  #error "board header must define BOARD_SCREEN_W / BOARD_SCREEN_H"
#endif
#ifndef BOARD_SCREEN_H
  #error "board header must define BOARD_SCREEN_W / BOARD_SCREEN_H"
#endif
#ifndef BOARD_TFT_ROTATION
  #error "board header must define BOARD_TFT_ROTATION"
#endif
#ifndef BOARD_TFT_INVERT
  #error "board header must define BOARD_TFT_INVERT (0 or 1)"
#endif
#ifndef BOARD_TOUCH_IS_I2C
  #error "board header must define BOARD_TOUCH_IS_I2C (0: XPT2046/SPI, 1: FT6336/I2C)"
#endif
#if !BOARD_TOUCH_IS_I2C
  #ifndef BOARD_TOUCH_SPI_BUS
    #error "board header must define BOARD_TOUCH_SPI_BUS (VSPI or HSPI)"
  #endif
#endif
#ifndef BOARD_TOUCH_ROTATION
  #error "board header must define BOARD_TOUCH_ROTATION"
#endif
#ifndef BOARD_HAS_BACKLIGHT_PWM
  #error "board header must define BOARD_HAS_BACKLIGHT_PWM (0 or 1)"
#endif
#ifndef BOARD_HAS_RGB_LED
  #error "board header must define BOARD_HAS_RGB_LED (0 or 1)"
#endif
#ifndef BOARD_LED_ACTIVE_LOW
  #error "board header must define BOARD_LED_ACTIVE_LOW (0 or 1)"
#endif

//  The constexpr half of the contract.  Naming every constant in one expression
//  makes a missing one an "undeclared identifier" HERE, beside the list above,
//  and costs nothing at runtime - the whole thing folds away at compile time.
//  Split by BOARD_TOUCH_IS_I2C because the two touch technologies do not share a
//  pin contract: an XPT2046 board never defines TOUCH_SDA, an FT6336 board never
//  defines TOUCH_CLK, and naming the wrong one here would itself be a compile
//  error next to this comment rather than useful.
#if BOARD_TOUCH_IS_I2C
static_assert(TOUCH_SDA < 40 && TOUCH_SCL < 40 && TOUCH_INT < 40 && TOUCH_RST < 40 &&
              LED_R < 40 && LED_G < 40 && LED_B < 40 &&
              I2C_SDA < 40 && I2C_SCL < 40,
              "board header: GPIO numbers must be 0-48 on the ESP32-S3 (this bound is the tighter ESP32 one, loosen if a pin above 39 is ever needed)");
#else
static_assert(TOUCH_CLK < 40 && TOUCH_MISO < 40 && TOUCH_MOSI < 40 &&
              TOUCH_CS  < 40 && TOUCH_IRQ  < 40 &&
              LED_R < 40 && LED_G < 40 && LED_B < 40 &&
              I2C_SDA < 40 && I2C_SCL < 40,
              "board header: GPIO numbers must be 0-39 on the ESP32");
#endif

static_assert(TOUCH_MAX_X != TOUCH_MIN_X && TOUCH_MAX_Y != TOUCH_MIN_Y,
              "board header: default touch mapping has a zero span");

static_assert(TOUCH_MIN_PRESSURE > 0 && I2C_HZ > 0,
              "board header: touch pressure and I2C clock must be non-zero");

//  A touch bus that is also the panel's is not a configuration, it is a bug: the
//  flush path leaves a DMA transfer in flight on purpose (see LvglPort.cpp), and
//  a touch read poking the same host mid-transfer corrupts the screen at random.
//  USE_HSPI_PORT is what puts the panel on HSPI, so it is the one comparison
//  that can be made here.  Only meaningful for BOARD_TOUCH_IS_I2C == 0 - an I2C
//  touch bus can never collide with the panel's SPI host.
//
//  The `defined(HSPI)` guard is load-bearing, not belt-and-braces: VSPI/HSPI are
//  macros from esp32-hal-spi.h, and this file deliberately includes no Arduino
//  header.  Without the guard, a translation unit that has not seen them would
//  compare two undefined identifiers - which the preprocessor reads as 0 == 0,
//  i.e. every board fails the check.  Ili9341Driver.cpp, the only place that
//  actually uses the value, includes SPI.h first, so the check does run where it
//  can mean something.
#if !BOARD_TOUCH_IS_I2C
  #if defined(USE_HSPI_PORT) && defined(HSPI) && (BOARD_TOUCH_SPI_BUS == HSPI)
    #error "Pins.h: the touch controller is on the panel's SPI host - see BOARD_TOUCH_SPI_BUS"
  #endif
#endif

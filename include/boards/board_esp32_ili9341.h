//*********************************************************************************
//  board_esp32_ili9341.h  -  ESP32 Dev Module + a separate 2.8" ILI9341 panel
//                            with an XPT2046 touch controller.
//
//  The discrete build: a plain ESP32-WROOM-32 devkit and the common red 2.8"
//  "TJCTM24028-SPI" style module (ILI9341 320x240 + XPT2046, both brought out on
//  the header).  Unlike the CYD, none of this is fixed by a factory - the pin
//  map below IS the wiring instruction, and the board must be wired to match.
//
//  Included ONLY through Pins.h, which is also what checks that every name
//  required of a board header is actually present.  Do not include it directly.
//
//  WIRING  (panel pins are TFT_eSPI build flags in platformio.ini, repeated here
//  so the whole harness can be read in one place):
//
//    Module     ESP32     Bus / note
//    ---------------------------------------------------------------------------
//    VCC        3V3       NOT 5V.  The logic is 3.3 V even on modules with a
//                         5 V-tolerant regulator marked on the silkscreen.
//    GND        GND
//    CS         GPIO 5    VSPI  (TFT_CS)
//    RESET      GPIO 4          (TFT_RST; tie to EN and set TFT_RST=-1 instead
//                                if the GPIO is needed elsewhere)
//    DC / RS    GPIO 2          (TFT_DC)
//    SDI / MOSI GPIO 23   VSPI  (TFT_MOSI)
//    SCK        GPIO 18   VSPI  (TFT_SCLK)
//    LED        GPIO 27         (TFT_BL, through the module's own series
//                                resistor; see BOARD_HAS_BACKLIGHT_PWM below if
//                                you would rather tie it to 3V3)
//    SDO / MISO GPIO 19   VSPI  (TFT_MISO)
//    T_CLK      GPIO 25   HSPI
//    T_CS       GPIO 33   HSPI
//    T_DIN      GPIO 32   HSPI  (touch MOSI)
//    T_DO       GPIO 39   HSPI  (touch MISO; input-only pin, which is exactly
//                                what a MISO line wants)
//    T_IRQ      GPIO 36         (input-only)
//
//    ADS1015    SDA GPIO 21, SCL GPIO 22  (the ESP32 Arduino default pair)
//
//  WHY THE TOUCH LINES GET THEIR OWN FOUR PINS.  The module ties T_DO/T_DIN/
//  T_CLK to the panel's SDO/SDI/SCK on some batches and brings them out
//  separately on others; this firmware wires them separately and puts the touch
//  controller on a second SPI host, because that is what lets the panel keep a
//  bus to itself for DMA flushing.  Sharing one bus would mean arbitrating every
//  transfer against an in-flight DMA push - the exact problem the CYD's layout
//  avoids by construction, and there is no reason to reintroduce it here.
//
//  PINS DELIBERATELY AVOIDED.  GPIO 6-11 are the flash.  GPIO 12 (MTDI) is
//  strapped at boot and a module that pulls it up will not start, which is why
//  the touch bus does not use the HSPI IO_MUX pins even though it is on the HSPI
//  host - at 2 MHz the GPIO matrix costs nothing.  GPIO 34-39 are input-only, so
//  only T_DO and T_IRQ are put there.
//*********************************************************************************
#pragma once

#include <stdint.h>

#define BOARD_NAME  "ESP32 Dev Module + ILI9341 2.8\""

//*********************************************************************************
//  Panel
//*********************************************************************************
//  Geometry AFTER rotation, which is what LVGL is told and what the whole UI is
//  laid out against.  Same 320x240 as the CYD, so no screen has to move.
#define BOARD_SCREEN_W        320
#define BOARD_SCREEN_H        240
#define BOARD_TFT_ROTATION    1      // landscape

//  A stock ILI9341 module is NOT colour-inverted; the CYD's panel is.  If this
//  board comes up in negative, this is the flag to flip - nothing else.
#define BOARD_TFT_INVERT      0

//*********************************************************************************
//  Touch  -  XPT2046 resistive, on the HSPI host.
//
//  The mirror image of the CYD: there the panel owns HSPI and touch gets VSPI,
//  here the panel owns VSPI (its IO_MUX pins 18/19/23 are the standard wiring
//  for these modules) and touch gets HSPI.  Either way the two never share a
//  host, which is the property that matters.
//*********************************************************************************
#define BOARD_TOUCH_SPI_BUS   HSPI
#define BOARD_TOUCH_ROTATION  1

constexpr uint8_t TOUCH_CLK  = 25;
constexpr uint8_t TOUCH_MISO = 39;
constexpr uint8_t TOUCH_MOSI = 32;
constexpr uint8_t TOUCH_CS   = 33;
constexpr uint8_t TOUCH_IRQ  = 36;

//  Raw-to-screen mapping.  These are the CYD's values as a starting point, and
//  on a discrete panel they are a guess - RUN THE TOUCH CALIBRATION SCREEN once
//  the board boots (Menu > Touch cal).  It solves a mapping from three presses
//  and stores it in NVS, after which these are only the fallback after a
//  settings reset.
//
//  If touch responds along the wrong axis, change BOARD_TOUCH_ROTATION; if it
//  responds along the right axis but backwards, swap MIN and MAX for that axis
//  (a reversed span maps correctly and is accepted - see
//  Ili9341Driver::setTouchCal).
constexpr uint16_t TOUCH_MIN_X        = 200;
constexpr uint16_t TOUCH_MAX_X        = 3700;
constexpr uint16_t TOUCH_MIN_Y        = 240;
constexpr uint16_t TOUCH_MAX_Y        = 3800;

//  Panel-dependent: raise it if the meter registers phantom taps, lower it if
//  light presses are ignored.
constexpr int16_t  TOUCH_MIN_PRESSURE = 300;

//*********************************************************************************
//  Backlight  -  TFT_BL (GPIO 27, set in platformio.ini) driven by LEDC.
//
//  Set this to 0 AND drop TFT_BL from the build flags if the module's LED pin is
//  tied straight to 3V3: the brightness menu then still stores a value but has
//  nothing to drive, which is a fixed backlight, not a fault.
//*********************************************************************************
#define BOARD_HAS_BACKLIGHT_PWM 1

//*********************************************************************************
//  Status LEDs  -  none on a bare devkit.
//
//  The meter is fully usable without them: the SWR alarm is on screen either
//  way, and the LEDs only ever repeated it.  To wire them anyway, set this to 1,
//  pick three free pins below, and set BOARD_LED_ACTIVE_LOW to match the way
//  they are wired - 0 for the usual GPIO -> resistor -> LED -> GND.
//*********************************************************************************
#define BOARD_HAS_RGB_LED     0
#define BOARD_LED_ACTIVE_LOW  0

constexpr uint8_t LED_R = 16;   // SWR alarm
constexpr uint8_t LED_G = 17;   // power detected
constexpr uint8_t LED_B = 26;   // unused

//*********************************************************************************
//  I2C bus for the ADS1015 converters.  The ESP32 Arduino default pair, free on
//  a devkit.  The addresses are NOT listed here: they are a property of the
//  chip, not of this board, and the meter discovers which ones are fitted by
//  probing - see Ads1015Detector::ADDRESSES.
//*********************************************************************************
constexpr uint8_t  I2C_SDA = 21;
constexpr uint8_t  I2C_SCL = 22;
constexpr uint32_t I2C_HZ  = 400000;

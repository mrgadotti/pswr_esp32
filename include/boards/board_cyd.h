//*********************************************************************************
//  board_cyd.h  -  ESP32-2432S028R "Cheap Yellow Display".
//
//  An integrated board: ESP32-WROOM-32, a 2.8" ILI9341 panel, an XPT2046 touch
//  controller and an RGB LED, all wired at the factory.  Nothing here is a
//  choice - it is a transcription of that wiring.
//
//  Included ONLY through Pins.h, which is also what checks that every name
//  required of a board header is actually present.  Do not include it directly.
//
//  The ILI9341's own pins are NOT here: they are TFT_eSPI build flags in
//  platformio.ini ([env:cyd]).  See the header of Pins.h for why that split
//  exists and is not going away.
//*********************************************************************************
#pragma once

#include <stdint.h>

#define BOARD_NAME  "ESP32-2432S028R (CYD)"

//*********************************************************************************
//  Panel
//*********************************************************************************
//  Geometry AFTER rotation, which is what LVGL is told and what the whole UI is
//  laid out against.
#define BOARD_SCREEN_W        320
#define BOARD_SCREEN_H        240
#define BOARD_TFT_ROTATION    1      // landscape, USB to the left

//  The CYD is fitted with a panel whose colours come out inverted, so the driver
//  has to undo it.  This is the single most confusing board difference there is:
//  get it wrong and the display works perfectly, in negative - which reads as a
//  broken colour theme rather than a one-line board setting.
#define BOARD_TFT_INVERT      1

//*********************************************************************************
//  Touch  -  XPT2046 resistive, on its OWN SPI bus.
//
//  The panel has HSPI to itself (USE_HSPI_PORT in platformio.ini), which is what
//  makes DMA flushing safe alongside touch reads, so the touch controller gets
//  VSPI.  Two hosts, no arbitration, no shared state.
//*********************************************************************************
#define BOARD_TOUCH_IS_I2C    0
#define BOARD_TOUCH_SPI_BUS   VSPI
#define BOARD_TOUCH_ROTATION  1

constexpr uint8_t TOUCH_CLK  = 25;
constexpr uint8_t TOUCH_MISO = 39;
constexpr uint8_t TOUCH_MOSI = 32;
constexpr uint8_t TOUCH_CS   = 33;
constexpr uint8_t TOUCH_IRQ  = 36;

//  Raw-to-screen mapping of the resistive panel.  A starting point only: panels
//  vary unit to unit, which is what the touch calibration screen is for - it
//  solves a new mapping and stores it in NVS, and from then on these values are
//  only ever the fallback after a settings reset.
constexpr uint16_t TOUCH_MIN_X        = 200;
constexpr uint16_t TOUCH_MAX_X        = 3700;
constexpr uint16_t TOUCH_MIN_Y        = 240;
constexpr uint16_t TOUCH_MAX_Y        = 3800;
constexpr int16_t  TOUCH_MIN_PRESSURE = 300;

//*********************************************************************************
//  Backlight  -  TFT_BL (GPIO 21, set in platformio.ini) driven by LEDC.
//*********************************************************************************
#define BOARD_HAS_BACKLIGHT_PWM 1

//*********************************************************************************
//  Status LEDs  -  onboard RGB, common anode, so a LOW lights it.
//*********************************************************************************
#define BOARD_HAS_RGB_LED     1
#define BOARD_LED_ACTIVE_LOW  1

constexpr uint8_t LED_R = 4;    // SWR alarm
constexpr uint8_t LED_G = 16;   // power detected
constexpr uint8_t LED_B = 17;   // unused

//*********************************************************************************
//  I2C bus for the ADS1015 converters (CYD CN1 free pins).  The addresses are
//  NOT listed here: they are a property of the chip, not of this board, and the
//  meter discovers which ones are fitted by probing - see
//  Ads1015Detector::ADDRESSES.
//*********************************************************************************
constexpr uint8_t  I2C_SDA = 27;
constexpr uint8_t  I2C_SCL = 22;
constexpr uint32_t I2C_HZ  = 400000;

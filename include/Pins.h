//*********************************************************************************
//  Pins.h  -  Hardware pin map and board addressing for the ESP32 "Cheap Yellow
//             Display" (CYD).  Shared across all modules.
//
//  NOTE: the ILI9341 (TFT) pins are build flags in platformio.ini, consumed by
//  TFT_eSPI at compile time under USER_SETUP_LOADED, so they are intentionally
//  NOT repeated here.  There is no User_Setup.h any more - see the TFT_eSPI
//  block in platformio.ini for why.
//*********************************************************************************
#pragma once

#include <stdint.h>

//  XPT2046 resistive touch on its dedicated VSPI bus (CYD wiring)
constexpr uint8_t TOUCH_CLK  = 25;
constexpr uint8_t TOUCH_MISO = 39;
constexpr uint8_t TOUCH_MOSI = 32;
constexpr uint8_t TOUCH_CS   = 33;
constexpr uint8_t TOUCH_IRQ  = 36;

//  Touch panel raw-to-screen mapping (calibration of the resistive panel)
constexpr uint16_t TOUCH_MIN_X        = 200;
constexpr uint16_t TOUCH_MAX_X        = 3700;
constexpr uint16_t TOUCH_MIN_Y        = 240;
constexpr uint16_t TOUCH_MAX_Y        = 3800;
constexpr int16_t  TOUCH_MIN_PRESSURE = 300;

//  CYD onboard RGB LED (active LOW)
constexpr uint8_t LED_R = 4;    // SWR alarm
constexpr uint8_t LED_G = 16;   // power detected
constexpr uint8_t LED_B = 17;   // unused

//  I2C bus for the ADS1015 converters (CYD CN1 free pins).  The addresses are
//  NOT listed here: they are a property of the chip, not of this board, and the
//  meter discovers which ones are fitted by probing - see
//  Ads1015Detector::ADDRESSES.
constexpr uint8_t  I2C_SDA = 27;
constexpr uint8_t  I2C_SCL = 22;
constexpr uint32_t I2C_HZ  = 400000;

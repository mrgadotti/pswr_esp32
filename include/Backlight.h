//*********************************************************************************
//  Backlight.h  -  Contract between the panel backlight and the settings UI.
//
//  Same reasoning as RawTouch.h: the brightness screen needs one method from the
//  display driver, and handing it an Ili9341Driver& would drag TFT_eSPI into
//  every translation unit that transitively includes UiApp.h.
//
//  Kept separate from RawTouch rather than bolted onto it because they are
//  genuinely different capabilities - a board could easily have one and not the
//  other, and a name that means "touch" should not grow a brightness method.
//
//  Deliberately dependency-free: stdint only.
//*********************************************************************************
#pragma once

#include <stdint.h>

struct Backlight {
  virtual ~Backlight() = default;

  //  Duty in percent.  Implementations clamp to a non-zero floor: see
  //  BRIGHTNESS_MIN in AppConfig.h for why going dark is not on offer.
  virtual void setBrightness(uint8_t percent) = 0;
};

//*********************************************************************************
//  RawTouch.h  -  Contract between the touch hardware and the calibration UI.
//
//  A calibration screen needs the UNMAPPED controller readings, which is the one
//  thing the normal touch path deliberately hides.  Exposing it through a tiny
//  pure-virtual interface (rather than handing the UI an Ili9341Driver&) keeps
//  TFT_eSPI out of every screen that transitively includes UiApp.h.
//
//  Deliberately dependency-free: stdint only, so it can live in include/ next to
//  Types.h and be implemented by the driver without either side pulling the
//  other's headers.
//*********************************************************************************
#pragma once

#include <stdint.h>

struct RawTouch {
  virtual ~RawTouch() = default;

  //  Raw controller counts, before any mapping.  Returns false when not pressed
  //  hard enough (same pressure gate as the mapped path).
  virtual bool readRaw(int& x, int& y) = 0;

  //  Install a new raw-to-screen mapping.  Takes effect immediately; persisting
  //  it is the caller's business.
  virtual void setTouchCal(int16_t minX, int16_t maxX, int16_t minY, int16_t maxY) = 0;
};

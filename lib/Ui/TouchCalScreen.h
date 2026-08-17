//*********************************************************************************
//  TouchCalScreen.h  -  Four-point calibration for the resistive panel.
//
//  Why this exists: the legacy firmware hard-coded the raw-to-screen mapping as
//  constants in Pins.h, and got away with it because every touch target was at
//  least 76 px wide.  Under LVGL the targets are 22 px menu rows and LVGL
//  hit-tests exactly, so the same few-percent error that was invisible before
//  makes the UI feel broken.
//
//  METHOD.  Show a crosshair at each of four inset targets, average several raw
//  readings per target, then solve the linear mapping.  Averaging the two
//  targets that share an axis (left pair for minX, right pair for maxX, and so
//  on) cancels most of the panel's skew.
//
//  Extrapolation is the point: the targets are INSET from the edges so the user
//  is never asked to press the very rim of the panel, where a resistive layer is
//  least linear.  The solve then extrapolates out to x=0 and x=SCREEN_W-1.
//
//  Deliberately NOT wired to a boot-time "is it calibrated?" check: the Pins.h
//  defaults are good enough to reach the menu, which is where this lives.  If a
//  panel ever ships so far off that the menu is unreachable, that assumption
//  breaks and this needs a boot-time escape hatch.
//*********************************************************************************
#pragma once

#include "RawTouch.h"
#include "Screen.h"

class TouchCalScreen : public Screen {
public:
  explicit TouchCalScreen(RawTouch* touch) : touch_(touch) {}

  void build() override;
  void onData(const MeterReadings& r, const Settings& s) override;

private:
  void showTarget();
  void finishPoint();
  void solveAndStore();

  static constexpr uint8_t POINTS  = 4;
  static constexpr uint8_t SAMPLES = 12;   // averaged per target
  static constexpr int16_t INSET   = 30;   // px from each edge

  RawTouch* touch_ = nullptr;

  lv_obj_t* hLine_   = nullptr;
  lv_obj_t* vLine_   = nullptr;
  lv_obj_t* lblHint_ = nullptr;

  //  Target screen coordinates, filled in build() from the live resolution.
  int16_t tx_[POINTS] = { 0 };
  int16_t ty_[POINTS] = { 0 };

  //  Accumulated raw readings for the target being measured.
  int32_t sumX_ = 0, sumY_ = 0;
  uint8_t got_  = 0;

  int32_t rawX_[POINTS] = { 0 };
  int32_t rawY_[POINTS] = { 0 };

  uint8_t point_ = 0;
  bool    done_  = false;
};

//*********************************************************************************
//  ModeIntroScreen.h  -  Brief banner naming the display mode just selected.
//
//  Pushed by MeterScreen when MODE is tapped; pops itself after MODE_INTRO_MS,
//  or immediately if the user taps.  Because it sits ON TOP of the meter rather
//  than replacing it, returning is a pop and the meter rebuilds itself for the
//  new mode in its own onEnter().
//*********************************************************************************
#pragma once

#include "Screen.h"

class ModeIntroScreen : public Screen {
public:
  explicit ModeIntroScreen(DisplayMode mode) : mode_(mode) {}

  void build() override;
  void onEnter() override;
  void onData(const MeterReadings& r, const Settings& s) override;

private:
  void dismiss();
  void onTap(lv_event_t* e);

  static constexpr uint32_t MODE_INTRO_MS = 1400;

  DisplayMode mode_;
  uint32_t    deadline_ = 0;
  bool        going_    = false;
};

//*********************************************************************************
//  BootScreen.h  -  The splash, and the boot deadline that follows it.
//
//  The legacy showBoot() was a delay(2500) inside setup(), which is also why the
//  meter task could only start afterwards.  Here the deadline is checked in
//  onData(), so the splash costs nothing and the measurement chain is already
//  running behind it.
//*********************************************************************************
#pragma once

#include "Screen.h"

class BootScreen : public Screen {
public:
  void build() override;
  void onEnter() override;
  void onData(const MeterReadings& r, const Settings& s) override;

private:
  static constexpr uint32_t SPLASH_MS = 2500;

  uint32_t deadline_ = 0;
  bool     handed_   = false;
};

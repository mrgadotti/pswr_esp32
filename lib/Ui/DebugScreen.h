//*********************************************************************************
//  DebugScreen.h  -  Raw detector voltages and derived figures.
//
//  A straight text dump, refreshed at ~150 ms as in the legacy version, exiting
//  on a tap anywhere.  The legacy runDebugScreen() was a for(;;) that had to
//  drain the sampler itself to keep the numbers moving; here it is just a screen
//  that reads the snapshot like any other.
//
//  This is also the ONLY reader of MeterReadings::avgPowerMw / avg1sPowerMw.
//  PowerMath has always computed them - they are part of the faithful port - but
//  nothing displayed them, so their 4.4 KB of sliding-window buffers were the
//  largest dead allocation in the firmware.  Deleting them was the alternative;
//  showing them wins because averaged power is a real measurement the meter face
//  deliberately does not carry, and one the operator occasionally needs.
//*********************************************************************************
#pragma once

#include "Screen.h"

class DebugScreen : public Screen {
public:
  void build() override;
  void onData(const MeterReadings& r, const Settings& s) override;

private:
  void onTap(lv_event_t* e);

  static constexpr uint8_t TICKS_REFRESH = 15;   // 15 * METER_TICK_MS = 150 ms

  lv_obj_t* lblVf_    = nullptr;
  lv_obj_t* lblVr_    = nullptr;
  lv_obj_t* lblDet_   = nullptr;
  lv_obj_t* lblPwr_   = nullptr;
  lv_obj_t* lblAvg_   = nullptr;
  lv_obj_t* lblAds_   = nullptr;

  uint32_t tick_  = 0;
  bool     going_ = false;
};

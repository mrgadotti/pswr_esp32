//*********************************************************************************
//  CalScreen.h  -  Detector calibration.  Two layouts, chosen by detector type.
//
//    AD8307 : four steppers (-10 / -1 / +1 / +10 dBm), then 1-LEVEL, SET P1,
//             SET P2, BACK.  A two-point log-amp calibration, gated on the
//             forward/reverse separation being large enough to trust.
//    Diode  : the same steppers acting on watts, then SET CAL, N:1, RESET,
//             BACK.  A single-point scale factor plus the coupler's
//             transformer turns ratio, which N:1 hands off to AdjustScreen
//             for - see openBridgeCoupling().
//
//  THE MATH IS LOAD-BEARING and is ported unchanged from the legacy
//  handleCalBtn(): what gets stored in calAd[]/meterCal has to come out
//  byte-identical, because an existing meter's calibration lives in NVS and a
//  silent change here would quietly mis-measure real RF.  Verify by running the
//  same input through old and new firmware and comparing the stored values.
//
//  One thing did change, deliberately: the legacy version captured the detector
//  voltages BEFORE its 90 ms button-flash delay, so a stored point was ~100 ms
//  stale.  Press feedback is now a style, with no delay at all, and the reading
//  used is the snapshot from the current tick - at most one tick old.
//*********************************************************************************
#pragma once

#include "Screen.h"

class CalScreen : public Screen {
public:
  void build() override;
  void onData(const MeterReadings& r, const Settings& s) override;

private:
  enum Quality : uint8_t { Bad = 0, Fwd = 1, Rev = 2 };

  void    buildButtons(bool ad8307);
  void    onButton(lv_event_t* e);
  void    flash(const char* msg, lv_color_t colour);
  void    refreshLive(const MeterReadings& r, const Settings& s);
  Quality quality(const MeterReadings& r) const;

  //  Diode-only: pushes an AdjustScreen for the selected coupler's transformer
  //  turns ratio (Calibration::bridgeCoupling).
  void        openBridgeCoupling();
  static void doneBridgeCoupling(void* ctx, int v);

  static constexpr uint8_t TICKS_REFRESH = 12;   // 12 * METER_TICK_MS = 120 ms
  static constexpr uint8_t MAX_BTN       = 8;

  lv_obj_t* lblVolts_ = nullptr;   // "Vf: x  Vr: y"
  lv_obj_t* lblState_ = nullptr;   // "FWD OK" / "REV" / "POOR sig" / "Meas: ..."
  lv_obj_t* lblP1_    = nullptr;
  lv_obj_t* lblP2_    = nullptr;
  lv_obj_t* lblRef_   = nullptr;   // "Ref: +40.0 dBm" / "Known: 80 W"
  lv_obj_t* lblFlash_ = nullptr;

  uint8_t buttonCount_ = 0;
  bool    isAd8307_    = true;
  bool    going_       = false;

  //  Reference level being calibrated against.  Seeded from the stored P1, as
  //  the legacy begin() did.
  int16_t refDb10m_ = 0;
  float   refWatts_ = 80.0f;

  uint32_t tick_ = 0;
};

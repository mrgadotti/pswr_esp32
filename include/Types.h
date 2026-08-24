//*********************************************************************************
//  Types.h  -  Shared enums and POD structs used across the modules.
//
//  Enum numeric values are kept identical to the original (DET_AD8307 = 0, the
//  display modes 1..4) so values persisted in NVS remain compatible.
//*********************************************************************************
#pragma once

#include <stdint.h>

#include <atomic>

//  Detector front-end type.
enum class DetectorType : uint8_t {
  AD8307 = 0,
  Diode  = 1,
};
constexpr DetectorType DETECTOR_DEFAULT = DetectorType::AD8307;

//  UI appearance.  A stored setting rather than a compile-time one because the
//  right answer depends on where the meter is standing: dark for a shack at
//  night, light for a bench under a window, and the same operator wants both.
enum class ThemeMode : uint8_t {
  Dark     = 0,
  Light    = 1,
  Dark2026 = 2,
};
constexpr ThemeMode THEME_DEFAULT = ThemeMode::Dark;

//  Highest valid value, for clamping what comes back from NVS.  Kept next to the
//  enum so adding a theme is one edit, not two - a stale bound here would silently
//  reset the new theme to Dark on every boot.
constexpr uint8_t THEME_MAX = static_cast<uint8_t>(ThemeMode::Dark2026);

//  Display modes (same numbering as the original; AnalogFwd = 5 is new and was
//  appended rather than inserted so a meter's NVS-stored 1..4 stays valid).
enum class DisplayMode : uint8_t {
  PowerBarPep     = 1,
  PowerMixed      = 2,
  PowerCleanDbm   = 3,
  ModulationScope = 4,
  AnalogFwd       = 5,
};
constexpr uint8_t DISPLAY_MODE_MIN = 1;
constexpr uint8_t DISPLAY_MODE_MAX = 5;

//  One AD8307 calibration point (was cal_t).
//
//  TWO dBm ANCHORS, ONE PER CHANNEL - the one place this struct departs from the
//  reference's cal_t, and it is a fix rather than a preference.
//
//  Forward and reverse are two INDEPENDENT two-point fits.  They happen to be
//  measured together when the coupler is fed forwards, which is why the original
//  could share a single db10m - but not when it is fed BACKWARDS, which is
//  exactly what the Calibrate screen's "REV" path exists for: there only `rev`
//  is measured.  With one shared anchor, storing that point also moved the
//  FORWARD fit's x-axis while leaving its stored volts untouched, so the forward
//  reading shifted by the whole difference between the two reference levels -
//  silently, and by 100x in the worst case (a 20 dBm rev calibration on a meter
//  whose forward pair was set at 40 dBm).  Now whichever channel's volts are
//  stored carries the dBm they were stored at.
//
//  Free in RAM: the padding after a lone int16_t was already 2 bytes wide, so
//  sizeof(CalPoint) is 12 either way.
//
//  NVS stays compatible - see ConfigManager::loadCal(), where revDb10m defaults
//  to db10m.  That default IS the old shared-anchor behaviour, so a meter
//  calibrated before this field existed loads reading exactly as it did.
struct CalPoint {
  int16_t db10m;      // dBm * 10 at which `fwd` was measured
  int16_t revDb10m;   // dBm * 10 at which `rev` was measured
  float   fwd;        // forward detector volts at db10m
  float   rev;        // reverse detector volts at revDb10m
};

//  How many couplers the meter can address: one per possible ADS1015 I2C
//  address (0x48..0x4B, strapped on the chip's ADDR pin).  That is what
//  physically distinguishes one coupler's front end from another's, so the
//  address count IS the coupler count - see Ads1015Detector::ADDRESSES.
constexpr uint8_t MAX_COUPLERS = 4;

//  Everything that has to be re-measured when the coupler changes.
//
//  Held PER COUPLER, not globally.  A directional coupler's turns ratio,
//  insertion loss and detector diodes are all baked into these numbers, so two
//  couplers on the same meter answer differently to the same RF.  One shared set
//  would silently apply coupler 1's calibration to coupler 2 - and the reading
//  would look perfectly plausible, which is the dangerous kind of wrong.
//
//  `detector` and `bridgeCoupling` live here, not in Settings, for the same
//  reason: which front end is bolted to a coupler and what its transformer's
//  turns ratio is are properties OF THAT COUPLER, not of the meter as a whole.
//  A meter can carry one AD8307 coupler and two differently-wound diode
//  bridges (e.g. 10:1 and 24:1) at once, and each must keep its own answer.
struct Calibration {
  DetectorType detector;      // which front end this coupler carries
  CalPoint calAd[2];          // AD8307: the two-point fit
  float    meterCal;          // diode / Bruene: the one-point scale factor
  double   bridgeCoupling;    // diode / Bruene: transformer turns ratio (N:1)
};

//  Persisted user settings (was the anonymous struct "R").
struct Settings {
  Calibration  cal[MAX_COUPLERS];
  uint8_t      swrAlarmTrig;        // SWR*10, 40 = off
  uint16_t     swrAlarmPwrThresh;   // mW
  uint8_t      scaleRange[3];
  DisplayMode  modeDisplay;
  uint8_t      pepIdx;              // index into PEP_OPTIONS / PEP labels

  //  1..MAX_COUPLERS, selects cal[] above.  The one field written by the UI task
  //  (MeterScreen) and read by the meter task (MeterEngine::taskLoop), so it is
  //  atomic rather than a plain byte: it is the only crossing between the two
  //  cores that carries a value, and std::atomic keeps that portable instead of
  //  leaning on the Xtensa's aligned-byte atomicity.
  std::atomic<uint8_t> coupler;
  //  0=HF 1=6M 2=2M 3=70cm.  Kept in NVS but no longer reachable from the UI:
  //  it only ever changed its own label, and the front panel row was needed for
  //  controls that do something.  Left in place because per-band calibration is
  //  the obvious next use for it, and dropping the field would burn the key.
  uint8_t      band;

  //  Resistive-panel calibration (raw controller counts at the screen edges).
  //  Defaults come from Pins.h; the Touch Calibrate screen overwrites them.
  int16_t      touchMinX, touchMaxX;
  int16_t      touchMinY, touchMaxY;

  //  Appearance.  brightness is a backlight duty in percent, floored well above
  //  zero - see BRIGHTNESS_MIN in AppConfig.h.
  ThemeMode    theme;
  uint8_t      brightness;

  //  Zero-based index of the selected coupler, always in range.  Clamped rather
  //  than asserted because the value comes from NVS: a meter that had three
  //  couplers fitted and now has one still boots, on coupler 1.
  uint8_t couplerIdx() const {
    return (coupler >= 1 && coupler <= MAX_COUPLERS) ? (uint8_t)(coupler - 1) : 0;
  }

  //  The calibration the math must use.  Every reader goes through here so that
  //  "which coupler is selected" is answered in exactly one place.
  Calibration&       activeCal()       { return cal[couplerIdx()]; }
  const Calibration& activeCal() const { return cal[couplerIdx()]; }
};

//  Live measurement results produced by PowerMath, published by MeterEngine and
//  read by the UI.
//  (Intermediates fInst/rInst/avg* are kept here too so the math is a faithful
//   port; the UI simply ignores the fields it does not need.)
struct MeterReadings {
  float  vFwd = 0, vRev = 0;
  bool   reverse = false;
  double fInst = 0, rInst = 0;
  double fwdPowerMw = 0, refPowerMw = 0;
  double netPowerMw = 0, netPowerDb = -90;
  double peakPowerDb = -90, peakPowerMw = 0;
  double pepPowerDb = -90, pepPowerMw = 0;
  double avgPowerMw = 0, avg1sPowerMw = 0;
  double ad8307FwdDbm = -90, ad8307RevDbm = -90;
  double swr = 1.0, swrAvg = 1.0;
  bool   swrAlarm = false;
  bool   powerDetected = false;
};

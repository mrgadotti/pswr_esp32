//*********************************************************************************
//  ConfigManager.h  -  Persists user settings in ESP32 NVS (Preferences),
//  applies defaults and validates values on load.  Owns the Settings struct.
//*********************************************************************************
#pragma once

#include <Preferences.h>

#include "Types.h"

class ConfigManager {
public:
  //  Apply defaults, open the NVS namespace and load + validate stored values.
  void begin();

  //  Persist the general settings / the touch panel calibration.
  void save();
  void saveTouchCal();

  //  Persist one coupler's calibration.  Defaults to the selected one, which is
  //  what the calibration screen just wrote into.
  void saveCal();
  void saveCal(uint8_t couplerIdx);

  //  Restore every setting to its compile-time default and persist it.
  void resetDefaults();

  Settings&       settings()       { return settings_; }
  const Settings& settings() const { return settings_; }

private:
  void loadDefaults();
  void loadCal(uint8_t couplerIdx);

  //  The compile-time calibration, applied to every coupler that has never been
  //  calibrated.  A default-valued coupler reads plausibly but not accurately -
  //  which is the honest starting point, and why the About screen names the
  //  coupler its figures belong to.
  static void defaultCal(Calibration& c);

  Preferences prefs_;
  Settings    settings_;
};

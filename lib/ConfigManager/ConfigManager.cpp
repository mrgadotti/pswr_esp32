//*********************************************************************************
//  ConfigManager.cpp
//*********************************************************************************
#include "ConfigManager.h"

#include <stdio.h>

#include "AppConfig.h"
#include "Pins.h"

//*********************************************************************************
//  CALIBRATION KEYS
//
//  Coupler 1 deliberately keeps the ORIGINAL, un-suffixed key names.  A meter
//  that was calibrated before per-coupler calibration existed therefore keeps
//  its numbers across this upgrade with no migration step and no chance of
//  quietly reverting to defaults - couplers 2..4 simply add a suffix.
//
//  (NVS keys are limited to 15 characters; the longest produced here is 5.)
//*********************************************************************************
static void calKey(char* out, size_t n, const char* base, uint8_t idx)
{
  if (idx == 0) snprintf(out, n, "%s", base);
  else          snprintf(out, n, "%s%u", base, (unsigned)(idx + 1));
}

void ConfigManager::defaultCal(Calibration& c)
{
  c.detector = DETECTOR_DEFAULT;

  //  Both anchors start equal: a factory-default meter has never been calibrated
  //  reverse-only, so its two fits share the same pair of reference levels.
  c.calAd[0]       = { CAL1_NOR_VALUE, CAL1_NOR_VALUE, CALFWD1_DEFAULT, CALREV1_DEFAULT };
  c.calAd[1]       = { CAL2_NOR_VALUE, CAL2_NOR_VALUE, CALFWD2_DEFAULT, CALREV2_DEFAULT };
  c.meterCal       = METER_CAL;
  c.bridgeCoupling = BRIDGE_COUPLING;
}

//  Mirrors the original setup() default block (note: modeDisplay is
//  intentionally NOT defaulted here - it is read from NVS with its own
//  default in begin(), exactly as in the original firmware).
void ConfigManager::loadDefaults()
{
  for (uint8_t i = 0; i < MAX_COUPLERS; i++) defaultCal(settings_.cal[i]);
  settings_.swrAlarmTrig      = SWR_ALARM_DEFAULT;
  settings_.swrAlarmPwrThresh = SWR_THRESHOLD_DEFAULT;
  settings_.scaleRange[0]     = SCALE_RANGE1;
  settings_.scaleRange[1]     = SCALE_RANGE2;
  settings_.scaleRange[2]     = SCALE_RANGE3;
  settings_.coupler           = 1;
  settings_.band              = 0;
  settings_.pepIdx            = 0;
  settings_.theme             = THEME_DEFAULT;
  settings_.brightness        = BRIGHTNESS_DEFAULT;
  settings_.touchMinX         = TOUCH_MIN_X;
  settings_.touchMaxX         = TOUCH_MAX_X;
  settings_.touchMinY         = TOUCH_MIN_Y;
  settings_.touchMaxY         = TOUCH_MAX_Y;
}

void ConfigManager::begin()
{
  loadDefaults();

  prefs_.begin("pswr", false);

  uint8_t mode = prefs_.getUChar("mode", DISPLAY_MODE_MIN);
  if (mode < DISPLAY_MODE_MIN || mode > DISPLAY_MODE_MAX) mode = DISPLAY_MODE_MIN;
  settings_.modeDisplay = static_cast<DisplayMode>(mode);

  settings_.pepIdx = prefs_.getUChar("pep", 0);
  if (settings_.pepIdx > 2) settings_.pepIdx = 0;

  //  Only range-checked here; MeterEngine::begin() narrows it further to the
  //  couplers that actually answered on the bus.
  settings_.coupler = prefs_.getUChar("cpl", 1);
  if (settings_.coupler < 1 || settings_.coupler > MAX_COUPLERS) settings_.coupler = 1;

  settings_.band = prefs_.getUChar("band", 0) & 3;

  //  Clamped, not masked.  The old `& 1` was a two-theme shortcut that would
  //  quietly turn any third theme into Dark - and it would do so on the boot
  //  AFTER the user selected it, which reads as "the setting does not stick".
  uint8_t thm = prefs_.getUChar("thm", static_cast<uint8_t>(THEME_DEFAULT));
  if (thm > THEME_MAX) thm = static_cast<uint8_t>(THEME_DEFAULT);
  settings_.theme = static_cast<ThemeMode>(thm);

  //  Clamped on load as well as on write: a value below the floor would come
  //  back as an unreadable panel, and the control to fix it is on that panel.
  settings_.brightness = prefs_.getUChar("bri", BRIGHTNESS_DEFAULT);
  if (settings_.brightness < BRIGHTNESS_MIN) settings_.brightness = BRIGHTNESS_MIN;
  if (settings_.brightness > BRIGHTNESS_MAX) settings_.brightness = BRIGHTNESS_MAX;

  settings_.swrAlarmTrig      = prefs_.getUChar("almt", SWR_ALARM_DEFAULT);
  settings_.swrAlarmPwrThresh = prefs_.getUShort("almp", SWR_THRESHOLD_DEFAULT);

  settings_.scaleRange[0] = prefs_.getUChar("sr0", SCALE_RANGE1);
  settings_.scaleRange[1] = prefs_.getUChar("sr1", SCALE_RANGE2);
  settings_.scaleRange[2] = prefs_.getUChar("sr2", SCALE_RANGE3);

  for (uint8_t i = 0; i < MAX_COUPLERS; i++) loadCal(i);

  //  Absent keys fall back to the Pins.h defaults, so an existing NVS namespace
  //  from before touch calibration existed still loads cleanly.
  settings_.touchMinX = prefs_.getShort("tx0", settings_.touchMinX);
  settings_.touchMaxX = prefs_.getShort("tx1", settings_.touchMaxX);
  settings_.touchMinY = prefs_.getShort("ty0", settings_.touchMinY);
  settings_.touchMaxY = prefs_.getShort("ty1", settings_.touchMaxY);
}

void ConfigManager::saveTouchCal()
{
  prefs_.putShort("tx0", settings_.touchMinX);
  prefs_.putShort("tx1", settings_.touchMaxX);
  prefs_.putShort("ty0", settings_.touchMinY);
  prefs_.putShort("ty1", settings_.touchMaxY);
}

void ConfigManager::save()
{
  prefs_.putUChar("mode", static_cast<uint8_t>(settings_.modeDisplay));
  prefs_.putUChar("pep",  settings_.pepIdx);
  prefs_.putUChar("cpl",  settings_.coupler);
  prefs_.putUChar("band", settings_.band);
  prefs_.putUChar("almt", settings_.swrAlarmTrig);
  prefs_.putUShort("almp", settings_.swrAlarmPwrThresh);
  prefs_.putUChar("sr0", settings_.scaleRange[0]);
  prefs_.putUChar("sr1", settings_.scaleRange[1]);
  prefs_.putUChar("sr2", settings_.scaleRange[2]);
  prefs_.putUChar("thm", static_cast<uint8_t>(settings_.theme));
  prefs_.putUChar("bri", settings_.brightness);
}

//  Absent keys keep whatever loadDefaults() put there, so a coupler that has
//  never been calibrated starts from the compile-time defaults rather than zero.
void ConfigManager::loadCal(uint8_t idx)
{
  Calibration& c = settings_.cal[idx];
  char k[8];

  //  "det", un-suffixed for coupler 1, is the SAME key the pre-per-coupler
  //  firmware wrote its one global detector choice under - so an existing
  //  meter's front end comes back as coupler 1's, with no migration step.
  calKey(k, sizeof(k), "det",  idx);
  c.detector = static_cast<DetectorType>(
      prefs_.getUChar(k, static_cast<uint8_t>(c.detector)));

  calKey(k, sizeof(k), "c0db", idx); c.calAd[0].db10m = prefs_.getShort(k, c.calAd[0].db10m);
  calKey(k, sizeof(k), "c0f",  idx); c.calAd[0].fwd   = prefs_.getFloat(k, c.calAd[0].fwd);
  calKey(k, sizeof(k), "c0r",  idx); c.calAd[0].rev   = prefs_.getFloat(k, c.calAd[0].rev);
  calKey(k, sizeof(k), "c1db", idx); c.calAd[1].db10m = prefs_.getShort(k, c.calAd[1].db10m);
  calKey(k, sizeof(k), "c1f",  idx); c.calAd[1].fwd   = prefs_.getFloat(k, c.calAd[1].fwd);
  calKey(k, sizeof(k), "c1r",  idx); c.calAd[1].rev   = prefs_.getFloat(k, c.calAd[1].rev);
  calKey(k, sizeof(k), "mcal", idx); c.meterCal       = prefs_.getFloat(k, c.meterCal);
  calKey(k, sizeof(k), "bcpl", idx); c.bridgeCoupling = prefs_.getDouble(k, c.bridgeCoupling);

  //  THE REVERSE ANCHORS, AND WHY THEY DEFAULT TO THE FORWARD ONES.
  //
  //  Read AFTER c0db/c1db above, because the fallback is the value just loaded
  //  rather than the compile-time constant - and that fallback is the whole
  //  upgrade path.  A meter calibrated before this key existed has no c0rd, so
  //  its reverse fit picks up the forward anchor, which is precisely the shared
  //  anchor the old firmware used.  It therefore reads identically after the
  //  update, with no migration step and nothing for the operator to redo; the
  //  two only diverge once a reverse-only calibration is actually performed.
  calKey(k, sizeof(k), "c0rd", idx); c.calAd[0].revDb10m = prefs_.getShort(k, c.calAd[0].db10m);
  calKey(k, sizeof(k), "c1rd", idx); c.calAd[1].revDb10m = prefs_.getShort(k, c.calAd[1].db10m);
}

void ConfigManager::saveCal()
{
  saveCal(settings_.couplerIdx());
}

void ConfigManager::saveCal(uint8_t idx)
{
  if (idx >= MAX_COUPLERS) return;

  const Calibration& c = settings_.cal[idx];
  char k[8];

  calKey(k, sizeof(k), "det",  idx); prefs_.putUChar(k, static_cast<uint8_t>(c.detector));
  calKey(k, sizeof(k), "c0db", idx); prefs_.putShort(k, c.calAd[0].db10m);
  calKey(k, sizeof(k), "c0f",  idx); prefs_.putFloat(k, c.calAd[0].fwd);
  calKey(k, sizeof(k), "c0r",  idx); prefs_.putFloat(k, c.calAd[0].rev);
  calKey(k, sizeof(k), "c1db", idx); prefs_.putShort(k, c.calAd[1].db10m);
  calKey(k, sizeof(k), "c1f",  idx); prefs_.putFloat(k, c.calAd[1].fwd);
  calKey(k, sizeof(k), "c1r",  idx); prefs_.putFloat(k, c.calAd[1].rev);
  calKey(k, sizeof(k), "mcal", idx); prefs_.putFloat(k, c.meterCal);
  calKey(k, sizeof(k), "bcpl", idx); prefs_.putDouble(k, c.bridgeCoupling);
  calKey(k, sizeof(k), "c0rd", idx); prefs_.putShort(k, c.calAd[0].revDb10m);
  calKey(k, sizeof(k), "c1rd", idx); prefs_.putShort(k, c.calAd[1].revDb10m);
}

void ConfigManager::resetDefaults()
{
  settings_.modeDisplay       = DisplayMode::PowerBarPep;
  settings_.pepIdx            = 0;
  settings_.coupler           = 1;
  settings_.band              = 0;
  settings_.swrAlarmTrig      = SWR_ALARM_DEFAULT;
  settings_.swrAlarmPwrThresh = SWR_THRESHOLD_DEFAULT;
  settings_.scaleRange[0]     = SCALE_RANGE1;
  settings_.scaleRange[1]     = SCALE_RANGE2;
  settings_.scaleRange[2]     = SCALE_RANGE3;
  settings_.touchMinX         = TOUCH_MIN_X;
  settings_.touchMaxX         = TOUCH_MAX_X;
  settings_.touchMinY         = TOUCH_MIN_Y;
  settings_.touchMaxY         = TOUCH_MAX_Y;
  settings_.theme             = THEME_DEFAULT;
  settings_.brightness        = BRIGHTNESS_DEFAULT;

  //  EVERY coupler, not just the selected one: "reset to default" that left
  //  three couplers holding old calibration would be a trap.
  for (uint8_t i = 0; i < MAX_COUPLERS; i++) defaultCal(settings_.cal[i]);

  save();
  for (uint8_t i = 0; i < MAX_COUPLERS; i++) saveCal(i);
  saveTouchCal();
}

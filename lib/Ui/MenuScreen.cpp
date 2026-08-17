//*********************************************************************************
//  MenuScreen.cpp
//*********************************************************************************
#include "MenuScreen.h"

#include <stdio.h>

#include "AboutScreen.h"
#include "AdjustScreen.h"
#include "AppConfig.h"
#include "CalScreen.h"
#include "DebugScreen.h"
#include "TouchCalScreen.h"
#include "UiApp.h"
#include "UiTheme.h"

//  Legacy table (DisplayManager.cpp), with the display settings added after the
//  panel calibration - everything that changes how the meter LOOKS grouped
//  together, and kept clear of the entries that change what it MEASURES.
static const char* const MENU_TOP[] = {
  "SWR Alarm", "SWR Alarm Power", "PEP Period", "Scale Ranges",
  "Detector", "Calibrate", "Debug Display", "Touch Calibrate",
  "Theme", "Brightness",
  "Reset to Default", "About", "Exit"
};
static constexpr int MENU_TOP_N = (int)(sizeof(MENU_TOP) / sizeof(MENU_TOP[0]));

static const uint16_t SWR_PWR_STEPS[] = { 1, 5, 10, 25, 50, 100, 200, 500 };
static constexpr int  SWR_PWR_N = (int)(sizeof(SWR_PWR_STEPS) / sizeof(SWR_PWR_STEPS[0]));

static const uint8_t SCALE_PRESETS[4][3] = {
  { 11, 22, 55 }, { 10, 20, 50 }, { 15, 30, 60 }, { 12, 24, 60 }
};

static const char* const PEP_LABELS[3] = { "100ms", "1s", "2.5s" };

MenuScreen::MenuScreen()
  : ListScreen("CONFIG", MENU_TOP, MENU_TOP_N, 0)
{
}

//*********************************************************************************
//  DISPATCH
//*********************************************************************************
void MenuScreen::finish(int sel)
{
  //  EXIT, or the "Exit" row: leave the menu entirely.
  if (sel < 0 || sel == MENU_TOP_N - 1) {
    if (app_) app_->requestPop();
    return;
  }

  switch (sel)
  {
    case 0: openSwrAlarm();       break;
    case 1: openSwrAlarmPower();  break;
    case 2: openPepPeriod();      break;
    case 3: openScaleRanges();    break;
    case 4: openDetector();       break;
    case 5: app_->requestPush(new CalScreen());   break;
    case 6: app_->requestPush(new DebugScreen()); break;
    case 7: app_->requestPush(new TouchCalScreen(&app_->touch())); break;
    case 8:  openTheme();          break;
    case 9:  openBrightness();     break;
    case 10: openResetDefaults();  break;
    case 11: app_->requestPush(new AboutScreen()); break;
    default: break;
  }
}

//*********************************************************************************
//  SUB-SCREENS  -  each is the "configure and push" half of a legacy menuXxx().
//*********************************************************************************
void MenuScreen::openSwrAlarm()
{
  Settings& s = app_->config().settings();
  int v = s.swrAlarmTrig;
  if (v < 10) v = 10;
  if (v > 40) v = 40;

  auto* scr = new AdjustScreen("SWR Alarm", "x.x : 1   (max = Off)",
                               v, 10, 40, 1, /*tenths*/ true, /*topLbl*/ "Off");
  scr->setOnDone(doneSwrAlarm, this);
  app_->requestPush(scr);
}

void MenuScreen::doneSwrAlarm(void* ctx, int v)
{
  auto* self = static_cast<MenuScreen*>(ctx);
  self->app_->config().settings().swrAlarmTrig = (uint8_t)v;
  self->app_->markSettingsDirty();
}

void MenuScreen::openSwrAlarmPower()
{
  Settings& s = app_->config().settings();

  char        bufs[SWR_PWR_N][ListScreen::MAX_LEN];
  const char* items[SWR_PWR_N];
  int         start = 0;

  for (int i = 0; i < SWR_PWR_N; i++) {
    snprintf(bufs[i], sizeof(bufs[i]), "%u mW", (unsigned)SWR_PWR_STEPS[i]);
    items[i] = bufs[i];
    if (SWR_PWR_STEPS[i] == s.swrAlarmPwrThresh) start = i;
  }

  //  ListScreen copies the strings, so these stack buffers can die here.
  auto* scr = new ListScreen("SWR Alarm Power", items, SWR_PWR_N, start);
  scr->setOnDone(doneSwrAlarmPower, this);
  app_->requestPush(scr);
}

void MenuScreen::doneSwrAlarmPower(void* ctx, int sel)
{
  if (sel < 0) return;
  auto* self = static_cast<MenuScreen*>(ctx);
  self->app_->config().settings().swrAlarmPwrThresh = SWR_PWR_STEPS[sel];
  self->app_->markSettingsDirty();
}

void MenuScreen::openPepPeriod()
{
  auto* scr = new ListScreen("PEP Period", PEP_LABELS, 3,
                             app_->config().settings().pepIdx);
  scr->setOnDone(donePepPeriod, this);
  app_->requestPush(scr);
}

void MenuScreen::donePepPeriod(void* ctx, int sel)
{
  if (sel < 0) return;
  auto* self = static_cast<MenuScreen*>(ctx);
  self->app_->config().settings().pepIdx = (uint8_t)sel;
  self->app_->markSettingsDirty();
}

void MenuScreen::openScaleRanges()
{
  Settings& s = app_->config().settings();

  char        bufs[4][ListScreen::MAX_LEN];
  const char* items[4];
  int         start = 0;

  for (int i = 0; i < 4; i++) {
    snprintf(bufs[i], sizeof(bufs[i]), "%u / %u / %u",
             (unsigned)SCALE_PRESETS[i][0], (unsigned)SCALE_PRESETS[i][1],
             (unsigned)SCALE_PRESETS[i][2]);
    items[i] = bufs[i];
    if (SCALE_PRESETS[i][0] == s.scaleRange[0] &&
        SCALE_PRESETS[i][1] == s.scaleRange[1] &&
        SCALE_PRESETS[i][2] == s.scaleRange[2]) start = i;
  }

  auto* scr = new ListScreen("Scale Ranges", items, 4, start);
  scr->setOnDone(doneScaleRanges, this);
  app_->requestPush(scr);
}

void MenuScreen::doneScaleRanges(void* ctx, int sel)
{
  if (sel < 0) return;
  auto* self = static_cast<MenuScreen*>(ctx);
  Settings& s = self->app_->config().settings();
  s.scaleRange[0] = SCALE_PRESETS[sel][0];
  s.scaleRange[1] = SCALE_PRESETS[sel][1];
  s.scaleRange[2] = SCALE_PRESETS[sel][2];
  self->app_->markSettingsDirty();
}

void MenuScreen::openDetector()
{
  static const char* const ITEMS[2] = { "2x AD8307 (log)", "Diode / Bruene" };
  const int start =
      (app_->config().settings().detector == DetectorType::AD8307) ? 0 : 1;

  auto* scr = new ListScreen("Detector", ITEMS, 2, start);
  scr->setOnDone(doneDetector, this);
  app_->requestPush(scr);
}

void MenuScreen::doneDetector(void* ctx, int sel)
{
  if (sel < 0) return;
  auto* self = static_cast<MenuScreen*>(ctx);
  self->app_->config().settings().detector =
      (sel == 0) ? DetectorType::AD8307 : DetectorType::Diode;
  self->app_->markSettingsDirty();
}

//*********************************************************************************
//  APPEARANCE
//*********************************************************************************
//  Order matters: the list index IS the ThemeMode value, which is what lets the
//  selected row come back without a lookup table to keep in step.
static const char* const THEME_ITEMS[] = { "Dark", "Light", "Dark 2026" };
static constexpr int THEME_COUNT = (int)(sizeof(THEME_ITEMS) / sizeof(THEME_ITEMS[0]));
static_assert(THEME_COUNT == THEME_MAX + 1, "theme list and ThemeMode disagree");

void MenuScreen::openTheme()
{
  int start = (int)app_->config().settings().theme;
  if (start < 0 || start >= THEME_COUNT) start = 0;

  auto* scr = new ListScreen("Theme", THEME_ITEMS, THEME_COUNT, start);
  scr->setOnDone(doneTheme, this);
  app_->requestPush(scr);
}

void MenuScreen::doneTheme(void* ctx, int sel)
{
  if (sel < 0 || sel >= THEME_COUNT) return;
  auto* self = static_cast<MenuScreen*>(ctx);

  self->app_->config().settings().theme = (ThemeMode)sel;
  self->app_->markSettingsDirty();

  //  Applied immediately, from inside the list's event dispatch.  Safe: this
  //  only rewrites style properties and invalidates - it creates and deletes
  //  nothing, so the object tree the dispatcher is still walking is untouched.
  UiTheme::applyTheme(self->app_->config().settings().theme);
}

void MenuScreen::openBrightness()
{
  auto* scr = new AdjustScreen("Brightness", "percent",
                               app_->config().settings().brightness,
                               BRIGHTNESS_MIN, BRIGHTNESS_MAX, BRIGHTNESS_STEP,
                               /*tenths*/ false, /*topLbl*/ nullptr);
  //  Live, not on confirmation: brightness is a setting whose whole feedback IS
  //  the effect, and choosing a number blind would be pointless.
  scr->setOnChange(changeBrightness, this);
  scr->setOnDone(doneBrightness, this);
  app_->requestPush(scr);
}

void MenuScreen::changeBrightness(void* ctx, int v)
{
  static_cast<MenuScreen*>(ctx)->app_->backlight().setBrightness((uint8_t)v);
}

void MenuScreen::doneBrightness(void* ctx, int v)
{
  auto* self = static_cast<MenuScreen*>(ctx);
  self->app_->config().settings().brightness = (uint8_t)v;
  self->app_->backlight().setBrightness((uint8_t)v);
  self->app_->markSettingsDirty();
}

void MenuScreen::openResetDefaults()
{
  static const char* const ITEMS[2] = { "No  -  keep settings", "Yes  -  reset all" };

  auto* scr = new ListScreen("Reset to Default", ITEMS, 2, 0);
  scr->setOnDone(doneResetDefaults, this);
  app_->requestPush(scr);
}

void MenuScreen::doneResetDefaults(void* ctx, int sel)
{
  if (sel != 1) return;
  //  resetDefaults() persists on its own, so no markSettingsDirty() here - and
  //  no 1200 ms blocking confirmation message either; the menu simply returns.
  static_cast<MenuScreen*>(ctx)->app_->config().resetDefaults();
}

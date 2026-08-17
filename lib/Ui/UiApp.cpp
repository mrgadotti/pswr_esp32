//*********************************************************************************
//  UiApp.cpp
//*********************************************************************************
#include "UiApp.h"

#include "AppConfig.h"
#include "UiTheme.h"

void UiApp::begin(ConfigManager& cfg, MeterEngine& meter, RawTouch& touch,
                  Backlight& backlight, Screen* first)
{
  cfg_       = &cfg;
  meter_     = &meter;
  touch_     = &touch;
  backlight_ = &backlight;

  //  Apply the stored panel calibration before the first screen can be touched,
  //  and the stored brightness before the first frame is visible.
  const Settings& s = cfg.settings();
  touch.setTouchCal(s.touchMinX, s.touchMaxX, s.touchMinY, s.touchMaxY);
  backlight.setBrightness(s.brightness);

  doPush(first);
}

//*********************************************************************************
//  NAVIGATION
//*********************************************************************************
void UiApp::requestPush(Screen* s)        { pendingPush_    = s; }
void UiApp::requestPop()                 { pendingPop_     = true; }
void UiApp::requestReplaceRoot(Screen* s) { pendingReplace_ = s; }

void UiApp::doPush(Screen* s)
{
  if (!s || depth_ >= MAX_DEPTH) return;

  s->attach(this);
  s->build();
  UiTheme::styleScreen(s->root());
  lv_scr_load(s->root());

  stack_[depth_++] = s;
  s->onEnter();
}

void UiApp::doPop()
{
  if (depth_ < 2) return;                 // never pop the root screen

  Screen* going = stack_[--depth_];
  Screen* below = stack_[depth_ - 1];

  //  Load FIRST, delete after.  LVGL refuses to delete the active screen, and
  //  even if it did not, deleting a displayed tree means the next refresh walks
  //  freed memory.
  lv_scr_load(below->root());
  going->onExit();
  delete going;

  stack_[depth_] = nullptr;
  below->onEnter();
}

//*********************************************************************************
//  PUMP
//*********************************************************************************
void UiApp::doReplaceRoot(Screen* s)
{
  if (!s || depth_ != 1) return;

  Screen* old = stack_[0];

  s->attach(this);
  s->build();
  UiTheme::styleScreen(s->root());
  lv_scr_load(s->root());        // load first, delete after - always

  old->onExit();
  delete old;

  stack_[0] = s;
  s->onEnter();
}

void UiApp::tick(uint32_t nowMs)
{
  //  Navigation first, so the rest of this tick already runs on the new screen.
  if (pendingPop_)     { pendingPop_ = false; doPop(); }
  if (pendingReplace_) { Screen* s = pendingReplace_; pendingReplace_ = nullptr; doReplaceRoot(s); }
  if (pendingPush_)    { Screen* s = pendingPush_;    pendingPush_    = nullptr; doPush(s); }

  if (!tickStarted_) { nextMeterTick_ = nowMs; tickStarted_ = true; }

  //  `while`, not `if`: a long frame catches up instead of dropping ticks -
  //  and dropping them would silently stretch the scope sweep and the autoscale
  //  hold, both of which are counted in ticks.
  while ((int32_t)(nowMs - nextMeterTick_) >= 0)
  {
    nextMeterTick_ += METER_TICK_MS;
    if ((int32_t)(nowMs - nextMeterTick_) > TICK_RESYNC_MS) nextMeterTick_ = nowMs;

    if (Screen* s = top()) s->onData(meter_->readings(), cfg_->settings());
  }

  if (saveDirty_ && (int32_t)(nowMs - saveDueAt_) >= 0)
  {
    saveDirty_ = false;
    cfg_->save();
  }
}

void UiApp::markSettingsDirty()
{
  saveDirty_ = true;
  saveDueAt_ = millis() + SAVE_DEBOUNCE_MS;
}

//*********************************************************************************
//  UiApp.h  -  Screen stack, data pump and settings persistence for the UI task.
//
//  Replaces the control flow that used to be six nested blocking for(;;) loops
//  in DisplayManager (waitNav / menuList / menuAdjust / enterConfigMenu /
//  runCalScreen / runDebugScreen).  The whole transformation is one sentence:
//  waitNav() returning an int becomes an event that mutates state.
//
//  DEFERRED NAVIGATION - the rule that prevents a whole class of crash.
//  An LVGL event callback must never delete the screen it is running on: the
//  dispatcher is still walking that object's parent chain when the callback
//  returns.  So requestPush()/requestPop() only set a pending request, and
//  tick() performs it at the top of the next UI iteration, outside any event
//  dispatch.  (LVGL's own answer is lv_async_call(); a pending flag costs the
//  same <=5 ms and keeps the transition point on one visible line.)
//
//  DEBOUNCED SAVE.  An NVS commit takes 10-50 ms.  In the old blocking loops
//  nobody noticed; under LVGL it stalls lv_timer_handler() and shows up as a
//  hitch on every menu confirmation.  markSettingsDirty() coalesces a burst of
//  changes into one commit - which also stops the legacy behaviour of writing
//  NVS on every single CPL / band / PEP tap.
//*********************************************************************************
#pragma once

#include <stdint.h>

#include "Backlight.h"
#include "MeterEngine.h"
#include "ConfigManager.h"
#include "RawTouch.h"
#include "Screen.h"

class UiApp {
public:
  void begin(ConfigManager& cfg, MeterEngine& meter, RawTouch& touch,
             Backlight& backlight, Screen* first);

  //  Call every UI iteration, before lv_timer_handler().
  void tick(uint32_t nowMs);

  //  Navigation.  Safe to call from inside an LVGL event callback: both are
  //  deferred to the next tick().
  void requestPush(Screen* s);
  void requestPop();

  //  Swap the ROOT screen, discarding the old one.  Used exactly once, when the
  //  boot splash hands over to the meter: a pop would not do, since there is
  //  nothing underneath the root to go back to.
  void requestReplaceRoot(Screen* s);

  //  Mark Settings as changed; the NVS commit happens SAVE_DEBOUNCE_MS after
  //  the last change.
  void markSettingsDirty();

  ConfigManager& config()    { return *cfg_; }
  MeterEngine&   meter()     { return *meter_; }
  RawTouch&      touch()     { return *touch_; }
  Backlight&     backlight() { return *backlight_; }

private:
  void doPush(Screen* s);
  void doPop();
  void doReplaceRoot(Screen* s);
  Screen* top() { return depth_ ? stack_[depth_ - 1] : nullptr; }

  //  Meter -> Menu -> {Adjust | Cal | Debug} is as deep as it goes.
  static constexpr uint8_t  MAX_DEPTH        = 4;
  static constexpr uint32_t SAVE_DEBOUNCE_MS = 200;
  //  If the data pump falls this far behind (a long NVS write, a full-screen
  //  repaint), resync instead of firing a burst of catch-up ticks.
  static constexpr int32_t  TICK_RESYNC_MS   = 50;

  ConfigManager* cfg_       = nullptr;
  MeterEngine*   meter_     = nullptr;
  RawTouch*      touch_     = nullptr;
  Backlight*     backlight_ = nullptr;

  Screen*  stack_[MAX_DEPTH] = { nullptr };
  uint8_t  depth_ = 0;

  Screen*  pendingPush_    = nullptr;
  Screen*  pendingReplace_ = nullptr;
  bool     pendingPop_     = false;

  uint32_t nextMeterTick_ = 0;
  bool     tickStarted_   = false;

  bool     saveDirty_   = false;
  uint32_t saveDueAt_   = 0;
};

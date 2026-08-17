//*********************************************************************************
//  Screen.h  -  Base class for every UI screen.
//
//  OWNERSHIP RULE, stated once and enforced everywhere:
//    Each Screen owns exactly one lv_obj_t* root, created with
//    lv_obj_create(nullptr).  The C++ object and the LVGL tree die together.
//
//  Do NOT use lv_scr_load_anim(..., auto_del = true).  It frees the old screen's
//  LVGL tree at a moment LVGL chooses, which leaves this object's cached child
//  pointers dangling while onData() may still be called on it.  UiApp deletes
//  explicitly, in a controlled order, and only outside LVGL's event dispatch.
//*********************************************************************************
#pragma once

#include <lvgl.h>

#include "Types.h"

class UiApp;

class Screen {
public:
  virtual ~Screen()
  {
    if (root_) lv_obj_del(root_);
  }

  void attach(UiApp* app) { app_ = app; }

  //  Create root_ and its children.  Called once, by UiApp::push().
  virtual void build() = 0;

  //  Screen became / stopped being the visible one.
  virtual void onEnter() {}
  virtual void onExit()  {}

  //  Fresh measurement data, at exactly METER_TICK_MS.  Screens that want a
  //  slower refresh count ticks here rather than starting an lv_timer: a timer
  //  that outlives its screen and touches freed widgets is the single most
  //  common LVGL crash, and counting makes that structurally impossible.
  virtual void onData(const MeterReadings&, const Settings&) {}

  lv_obj_t* root() const { return root_; }

protected:
  lv_obj_t* root_ = nullptr;
  UiApp*    app_  = nullptr;
};

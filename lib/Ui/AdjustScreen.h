//*********************************************************************************
//  AdjustScreen.h  -  Big-number integer adjuster.  Replaces menuAdjust().
//
//  UP increases, DOWN decreases, ENTER and EXIT both confirm (as in the legacy
//  version - there is no cancel here, deliberately).
//
//  `tenths` renders the value as x.y, which is how the SWR alarm threshold is
//  stored (SWR * 10).  `topLbl`, when set, replaces the number once the value
//  reaches `hi` - that is how "Off" is shown for a disabled alarm, and it is
//  also why the legacy code switched from the digits-only 48 px font to a text
//  font at that one value.  With Montserrat that special case disappears: one
//  font renders both.
//*********************************************************************************
#pragma once

#include "NavColumn.h"
#include "Screen.h"

class AdjustScreen : public Screen {
public:
  using DoneFn   = void (*)(void* ctx, int value);
  using ChangeFn = void (*)(void* ctx, int value);

  AdjustScreen(const char* title, const char* unit, int value,
               int lo, int hi, int step, bool tenths, const char* topLbl);

  void setOnDone(DoneFn fn, void* ctx) { doneFn_ = fn; doneCtx_ = ctx; }

  //  Fires on every step, before confirmation.  For settings whose effect IS
  //  the feedback - backlight brightness being the obvious one, where picking a
  //  number blind and only seeing it after leaving the screen would be absurd.
  //  The sink must be cheap: this runs inside the button's event dispatch.
  void setOnChange(ChangeFn fn, void* ctx) { changeFn_ = fn; changeCtx_ = ctx; }

  void build() override;

private:
  static void navCb(void* ctx, uint8_t key);
  void        onNav(uint8_t key);
  void        refresh();

  char title_[24]  = { 0 };
  char unit_[32]   = { 0 };
  char topLbl_[12] = { 0 };
  bool hasTopLbl_  = false;

  int  value_ = 0, lo_ = 0, hi_ = 0, step_ = 1;
  bool tenths_ = false;

  lv_obj_t* lblValue_ = nullptr;
  NavColumn nav_;

  DoneFn   doneFn_    = nullptr;
  void*    doneCtx_   = nullptr;
  ChangeFn changeFn_  = nullptr;
  void*    changeCtx_ = nullptr;
};

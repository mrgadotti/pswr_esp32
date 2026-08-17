//*********************************************************************************
//  MenuScreen.h  -  The CONFIG menu.  Replaces enterConfigMenu().
//
//  The legacy version was a for(;;) that called a blocking sub-function per
//  entry and looped when it returned.  Here the dispatch happens in finish():
//  instead of calling something that blocks, it pushes a child screen and
//  returns immediately, and the child delivers its result through a sink.
//
//  MenuScreen therefore stays on the stack underneath its children, which is
//  what makes "back to the menu, with the same row still selected" free.
//*********************************************************************************
#pragma once

#include "ListScreen.h"

class MenuScreen : public ListScreen {
public:
  MenuScreen();

protected:
  //  ENTER on a row: push the matching sub-screen instead of popping.
  void finish(int sel) override;

private:
  void openSwrAlarm();
  void openSwrAlarmPower();
  void openPepPeriod();
  void openScaleRanges();
  void openDetector();
  void openTheme();
  void openBrightness();
  void openResetDefaults();

  //  Result sinks.  Static thunks + `this`, which is the pattern that lets a
  //  child screen hand a value back without knowing anything about its parent.
  static void doneSwrAlarm(void* ctx, int v);
  static void doneSwrAlarmPower(void* ctx, int sel);
  static void donePepPeriod(void* ctx, int sel);
  static void doneScaleRanges(void* ctx, int sel);
  static void doneDetector(void* ctx, int sel);
  static void doneTheme(void* ctx, int sel);
  static void doneResetDefaults(void* ctx, int sel);

  //  Brightness is the one setting applied while it is being chosen, so it has
  //  a second sink for the intermediate values.
  static void changeBrightness(void* ctx, int v);
  static void doneBrightness(void* ctx, int v);
};

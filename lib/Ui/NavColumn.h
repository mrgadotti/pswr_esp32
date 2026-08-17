//*********************************************************************************
//  NavColumn.h  -  The UP / ENTER / DOWN column plus the EXIT button.
//
//  This is what replaces waitNav(): the legacy version was a for(;;) that
//  polled the touch panel every 15 ms and returned an int, freezing the whole
//  UI until the user pressed something.  Here the same four buttons simply emit
//  a key and the caller mutates state.
//
//  KEPT, rather than replaced by pure touch gestures, on purpose: it is the
//  only input that works reliably on a resistive panel with wet or gloved
//  hands, AdjustScreen reuses it where there is no list to scroll (so removing
//  it would mean designing a second increment UI), and it preserves the
//  encoder-style muscle memory of the TF3LJ reference firmware.  Touch scrolling
//  is added alongside it in ListScreen, not instead of it.
//
//  Geometry is the legacy navBtn[] table, unchanged.
//*********************************************************************************
#pragma once

#include <lvgl.h>

class NavColumn {
public:
  enum Key : uint8_t { Up = 0, Enter = 1, Down = 2, Exit = 3 };

  using Fn = void (*)(void* ctx, uint8_t key);

  void create(lv_obj_t* parent, Fn fn, void* ctx);

private:
  static void onClick(lv_event_t* e);

  Fn    fn_  = nullptr;
  void* ctx_ = nullptr;
};

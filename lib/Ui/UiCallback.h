//*********************************************************************************
//  UiCallback.h  -  Binding LVGL's C callbacks to C++ member functions.
//
//  The member pointer is a TEMPLATE argument, so this compiles to a direct call:
//  no std::function, no heap, no vtable, no runtime dispatch.
//
//      lv_obj_add_event_cb(btn, lvBind<MeterScreen, &MeterScreen::onButton>,
//                          LV_EVENT_CLICKED, this);
//
//  Two traps worth stating once, because both fail confusingly:
//
//  1. A capturing lambda will NOT convert to lv_event_cb_t.  Non-capturing ones
//     will, and can read lv_event_get_user_data() - so anything that "needs" a
//     capture should pass it as user_data instead.
//
//  2. lv_event_get_user_data(e) and lv_obj_get_user_data(obj) are different
//     things.  The first is the 4th argument of lv_obj_add_event_cb (here: the
//     receiving C++ object); the second is set separately on the widget (here:
//     typically an index).  Used together they give both the receiver and a
//     discriminator with no lookup table - see lvIndexOf().
//*********************************************************************************
#pragma once

#include <lvgl.h>

#include <stdint.h>

template <class T, void (T::*Method)(lv_event_t*)>
void lvBind(lv_event_t* e)
{
  (static_cast<T*>(lv_event_get_user_data(e))->*Method)(e);
}

//  Stash a small integer on a widget, and read it back inside a shared handler.
inline void lvSetIndex(lv_obj_t* obj, uint8_t i)
{
  lv_obj_set_user_data(obj, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
}

inline uint8_t lvIndexOf(lv_event_t* e)
{
  return static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target(e))));
}

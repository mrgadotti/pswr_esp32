//*********************************************************************************
//  ListScreen.h  -  Scrolling single-choice list.  Replaces menuList().
//
//  The legacy menuList() was a for(;;) around waitNav(), redrawing nine rows by
//  hand on every keypress and using a fillRect both as the highlight and as the
//  eraser.  Here the rows are objects: UP/DOWN move the selection, ENTER
//  confirms, EXIT cancels - and because the row container is scrollable, the
//  panel can also be dragged, which the old one could not do at all.
//
//  A side effect worth noting: the legacy `visible = (SCREEN_H - LIST_Y0) /
//  LIST_RH` = 9 was a hard ceiling on how many entries a menu could have.  The
//  list scrolls now, so menus can grow without touching layout code.
//
//  RESULT DELIVERY.  The caller supplies a sink (fn + ctx) rather than polling
//  a public member after the pop, so each legacy menuXxx() splits cleanly into
//  "configure and push" and "on result".  `sel` is -1 when the user exits.
//
//  Item strings are COPIED into the screen.  Several callers build them in
//  stack buffers (the mW steps, the scale presets) which would be long gone by
//  the time a row is drawn.
//*********************************************************************************
#pragma once

#include "NavColumn.h"
#include "Screen.h"

class ListScreen : public Screen {
public:
  //  The config menu is the longest list at 13 entries; the headroom is there
  //  so that adding a setting is a one-line change.  Cost is MAX_ITEMS * MAX_LEN
  //  bytes per open list screen - see the memory notes in AppConfig.h.
  static constexpr int MAX_ITEMS = 16;
  static constexpr int MAX_LEN   = 20;

  using DoneFn = void (*)(void* ctx, int sel);

  ListScreen(const char* title, const char* const* items, int n, int startSel);

  void setOnDone(DoneFn fn, void* ctx) { doneFn_ = fn; doneCtx_ = ctx; }

  void build() override;

protected:
  //  Called on ENTER (sel >= 0) or EXIT (sel < 0).  The default implementation
  //  invokes the sink and pops; MenuScreen overrides it to stay on screen and
  //  push a sub-screen instead.
  virtual void finish(int sel);

  int  selected() const { return sel_; }

private:
  static void navCb(void* ctx, uint8_t key);
  void        onNav(uint8_t key);
  void        onRow(lv_event_t* e);
  void        applySelection();

  char items_[MAX_ITEMS][MAX_LEN] = { { 0 } };
  int  count_ = 0;
  int  sel_   = 0;

  char title_[24] = { 0 };

  lv_obj_t*  rows_[MAX_ITEMS] = { nullptr };
  lv_obj_t*  listBox_ = nullptr;
  NavColumn  nav_;

  DoneFn doneFn_  = nullptr;
  void*  doneCtx_ = nullptr;
};

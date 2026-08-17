//*********************************************************************************
//  NavColumn.cpp
//*********************************************************************************
#include "NavColumn.h"

#include "UiCallback.h"
#include "UiTheme.h"

namespace {
struct NavBtnDef { lv_coord_t x, y, w, h; const char* label; };

//  Legacy navBtn[] geometry, verbatim.
const NavBtnDef DEFS[4] = {
  {   4,  38, 76, 48, "UP"    },
  {   4,  90, 76, 48, "ENTER" },
  {   4, 142, 76, 48, "DOWN"  },
  { 240, 190, 76, 44, "EXIT"  },
};
}  // namespace

void NavColumn::create(lv_obj_t* parent, Fn fn, void* ctx)
{
  fn_  = fn;
  ctx_ = ctx;

  for (uint8_t i = 0; i < 4; i++)
  {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &UiTheme::styleBtn, 0);
    lv_obj_add_style(b, &UiTheme::styleBtnPressed, LV_STATE_PRESSED);
    lv_obj_set_pos(b, DEFS[i].x, DEFS[i].y);
    lv_obj_set_size(b, DEFS[i].w, DEFS[i].h);
    lvSetIndex(b, i);
    lv_obj_add_event_cb(b, onClick, LV_EVENT_CLICKED, this);

    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, DEFS[i].label);
    lv_obj_center(l);
  }
}

void NavColumn::onClick(lv_event_t* e)
{
  NavColumn* self = static_cast<NavColumn*>(lv_event_get_user_data(e));
  if (self->fn_) self->fn_(self->ctx_, lvIndexOf(e));
}

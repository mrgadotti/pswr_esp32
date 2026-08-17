//*********************************************************************************
//  BootScreen.cpp
//*********************************************************************************
#include "BootScreen.h"

#include <Arduino.h>

#include "AppConfig.h"
#include "MeterScreen.h"
#include "UiApp.h"
#include "UiTheme.h"

void BootScreen::build()
{
  root_ = lv_obj_create(nullptr);
  UiTheme::styleScreen(root_);

  const lv_coord_t w = lv_disp_get_hor_res(nullptr);

  auto line = [&](const char* txt, const lv_font_t* font, lv_color_t colour,
                  lv_coord_t y) {
    lv_obj_t* l = lv_label_create(root_);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, colour, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, w);
    lv_obj_set_pos(l, 0, y);
    lv_label_set_text(l, txt);
    return l;
  };

  line("PSWR Meter",                    UI_FONT_LARGE, UI_YELLOW, 40);
  line("RF Power & SWR",                UI_FONT_MED,   UI_CYAN,   72);
  line("Marcelo R. Gadotti  -  PP5MGT", UI_FONT_MED,   UI_WHITE,  110);
  line("v" VERSION,                     UI_FONT_MED,   UI_DKGREY, 134);

  //  The reference work is credited here rather than only in the source: the
  //  measurement algorithms come from it, and GPL v3 asks that this not be
  //  quietly dropped.
  line("Measurement after TF3LJ / VE2AO", UI_FONT_SMALL, UI_GREY, 162);
  line("GPL v3",            UI_FONT_SMALL, UI_DKGREY, 176);

  //  Filled in onEnter(), once the ADS-present state can be read.
  line("", UI_FONT_MED, UI_GREY, 194);
}

void BootScreen::onEnter()
{
  deadline_ = millis() + SPLASH_MS;

  //  The status line is the last child added in build().
  lv_obj_t* status = lv_obj_get_child(root_, -1);
  const bool present = app_->meter().detectorPresent();
  lv_label_set_text(status, present ? "ADS1015 detected"
                                    : "ADS1015 not found - simulated signal");
  lv_obj_set_style_text_color(status, present ? UI_GREEN : UI_SIM, 0);
}

void BootScreen::onData(const MeterReadings& r, const Settings& s)
{
  LV_UNUSED(r);
  LV_UNUSED(s);

  if (handed_ || (int32_t)(millis() - deadline_) < 0) return;

  //  Guard against re-entry: requestReplaceRoot() is deferred, so onData() can
  //  fire again before the swap actually happens.
  handed_ = true;
  app_->requestReplaceRoot(new MeterScreen());
}

//*********************************************************************************
//  ModeIntroScreen.cpp
//*********************************************************************************
#include "ModeIntroScreen.h"

#include <Arduino.h>

#include "UiApp.h"
#include "UiCallback.h"
#include "UiTheme.h"

//  Legacy modeName1/modeName2 tables, indexed by DisplayMode (1..5).  Index 5
//  (AnalogFwd) is new; 1..4 are the legacy modeName1/modeName2 strings verbatim.
static const char* const NAME1[6] = { "", "Peak Envelope", "Forward and",
                                      "dBm Meter", "Modulation", "Analog" };
static const char* const NAME2[6] = { "", "Power Bargraph", "Reflected Power",
                                      "and SWR Bargraph", "Scope", "Forward Meter" };

void ModeIntroScreen::build()
{
  root_ = lv_obj_create(nullptr);
  UiTheme::styleScreen(root_);
  lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(root_, lvBind<ModeIntroScreen, &ModeIntroScreen::onTap>,
                      LV_EVENT_CLICKED, this);

  const lv_coord_t w = lv_disp_get_hor_res(nullptr);

  //  mode_ is clamped to 1..DISPLAY_MODE_MAX at the source (ConfigManager), but
  //  NAME1/NAME2 are only 6 entries (0..5), so clamp here too rather than
  //  masking with & 7 - a mask wraps mod 8 and would still index out of bounds
  //  for a corrupted 6..7.
  const uint8_t m = ((uint8_t)mode_ >= 1 && (uint8_t)mode_ <= DISPLAY_MODE_MAX) ? (uint8_t)mode_ : 1;

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

  line("Mode:", UI_FONT_MED, UI_WHITE, 52);
  line(NAME1[m], UI_FONT_LARGE, UI_YELLOW, 88);
  line(NAME2[m], UI_FONT_LARGE, UI_YELLOW, 118);

  if (!app_->meter().detectorPresent())
    line("** SIMULATED SIGNAL **", UI_FONT_MED, UI_SIM, 174);
}

void ModeIntroScreen::onEnter()
{
  deadline_ = millis() + MODE_INTRO_MS;
}

void ModeIntroScreen::onData(const MeterReadings& r, const Settings& s)
{
  LV_UNUSED(r);
  LV_UNUSED(s);
  if ((int32_t)(millis() - deadline_) >= 0) dismiss();
}

void ModeIntroScreen::onTap(lv_event_t* e)
{
  LV_UNUSED(e);
  dismiss();
}

void ModeIntroScreen::dismiss()
{
  if (going_) return;      // pop is deferred; do not queue it twice
  going_ = true;
  app_->requestPop();
}

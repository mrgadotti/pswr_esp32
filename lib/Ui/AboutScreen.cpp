//*********************************************************************************
//  AboutScreen.cpp
//*********************************************************************************
#include "AboutScreen.h"

#include <stdio.h>

#include "AppConfig.h"
#include "Pins.h"
#include "UiApp.h"
#include "UiCallback.h"
#include "UiFormat.h"
#include "UiTheme.h"

lv_obj_t* AboutScreen::addLine(lv_obj_t* parent, const char* txt,
                               const lv_font_t* font, lv_color_t colour)
{
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, colour, 0);
  lv_obj_set_width(l, LV_SIZE_CONTENT);
  lv_label_set_text(l, txt);
  return l;
}

void AboutScreen::build()
{
  const Settings& s  = app_->config().settings();
  const bool  ad8307 = (s.activeCal().detector == DetectorType::AD8307);

  root_ = lv_obj_create(nullptr);
  UiTheme::styleScreen(root_);
  lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(root_, lvBind<AboutScreen, &AboutScreen::onTap>,
                      LV_EVENT_CLICKED, this);

  const lv_coord_t w = lv_disp_get_hor_res(nullptr);
  const lv_coord_t h = lv_disp_get_ver_res(nullptr);

  lv_obj_t* title = addLine(root_, "ABOUT", UI_FONT_LARGE, UI_YELLOW);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

  //  A scrolling body rather than absolute positions: this screen accumulates
  //  lines over time (a new calibrated parameter, another credit), and a fixed
  //  layout would silently push the last one off the bottom.
  lv_obj_t* body = lv_obj_create(root_);
  lv_obj_remove_style_all(body);
  lv_obj_add_style(body, &UiTheme::stylePanel, 0);
  lv_obj_set_pos(body, 8, 32);
  lv_obj_set_size(body, w - 16, h - 32 - 20);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, 2, 0);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

  char b[56];

  //  ---- identity ------------------------------------------------------------
  snprintf(b, sizeof(b), "PSWR Meter   v%s", VERSION);
  addLine(body, b, UI_FONT_MED, UI_WHITE);
  addLine(body, "Marcelo R. Gadotti  -  PP5MGT", UI_FONT_SMALL, UI_CYAN);
  addLine(body, "GPL v3", UI_FONT_SMALL, UI_GREY);

  //  Which board this binary was built for.  Worth a line now that there is
  //  more than one: the two builds are visually identical until something is
  //  wired differently, and "wrong firmware for this hardware" is otherwise
  //  diagnosed by staring at a dead touch panel.
  addLine(body, "Board: " BOARD_NAME, UI_FONT_SMALL, UI_GREY);

  //  ---- front end -----------------------------------------------------------
  addLine(body, "", UI_FONT_SMALL, UI_GREY);
  addLine(body, ad8307 ? "Bridge: 2x AD8307 (log detector)"
                       : "Bridge: Diode / Bruene",
          UI_FONT_SMALL, UI_YELLOW);

  //  "fitted" vs "simulated" comes from detectorPresent(), not from the count:
  //  with no ADS1015 on the bus the mock still offers two couplers.
  const uint8_t couplers = app_->meter().couplerCount();
  const bool    real     = app_->meter().detectorPresent();

  snprintf(b, sizeof(b), "Couplers: %u %s, using CPL%u",
           (unsigned)couplers, real ? "fitted" : "simulated",
           (unsigned)s.coupler);

  addLine(body, b, UI_FONT_SMALL, real ? UI_GREEN : UI_SIM);

  //  ---- calibrated parameters ----------------------------------------------
  //  Only the ones the ACTIVE detector actually uses, for the ACTIVE coupler:
  //  showing any other set would invite someone to read numbers that are not in
  //  play.  The heading names the coupler for the same reason.
  addLine(body, "", UI_FONT_SMALL, UI_GREY);
  snprintf(b, sizeof(b), "Calibration  -  CPL%u", (unsigned)s.coupler);
  addLine(body, b, UI_FONT_SMALL, UI_YELLOW);

  const Calibration& c = s.activeCal();

  if (ad8307)
  {
    //  Forward and reverse can sit at different reference levels once the meter
    //  has been calibrated reverse-only, so the level is printed per channel
    //  whenever the two disagree - see the note on CalPoint in Types.h.
    UiFormat::calPoint(b, sizeof(b), "P1", c.calAd[0]);
    addLine(body, b, UI_FONT_SMALL, UI_CYAN);

    UiFormat::calPoint(b, sizeof(b), "P2", c.calAd[1]);
    addLine(body, b, UI_FONT_SMALL, UI_CYAN);

    //  The slope is what the two points actually mean; a wrong-looking value
    //  here is the fastest way to spot a bad calibration.  Both channels get
    //  one, because with split anchors they are genuinely two different lines
    //  and a single "fwd" figure would hide a bad reverse fit entirely.
    const double ddbF = (c.calAd[1].db10m    - c.calAd[0].db10m)    / 10.0;
    const double ddbR = (c.calAd[1].revDb10m - c.calAd[0].revDb10m) / 10.0;

    if (ddbF != 0) {
      snprintf(b, sizeof(b), "Slope  %.1f mV/dB fwd",
               1000.0 * (c.calAd[1].fwd - c.calAd[0].fwd) / ddbF);
      addLine(body, b, UI_FONT_SMALL, UI_GREY);
    }
    if (ddbR != 0) {
      snprintf(b, sizeof(b), "Slope  %.1f mV/dB rev",
               1000.0 * (c.calAd[1].rev - c.calAd[0].rev) / ddbR);
      addLine(body, b, UI_FONT_SMALL, UI_GREY);
    }
  }
  else
  {
    snprintf(b, sizeof(b), "meter_cal  %.4f", c.meterCal);
    addLine(body, b, UI_FONT_SMALL, UI_CYAN);
    snprintf(b, sizeof(b), "Coupling  %.0f:1    Vdrop  %.2f V",
             c.bridgeCoupling, D_VDROP);
    addLine(body, b, UI_FONT_SMALL, UI_CYAN);
  }

  //  ---- other stored settings worth seeing ---------------------------------
  addLine(body, "", UI_FONT_SMALL, UI_GREY);
  if (s.swrAlarmTrig == 40)
    snprintf(b, sizeof(b), "SWR alarm  off");
  else
    snprintf(b, sizeof(b), "SWR alarm  %u.%u:1 above %u mW",
             (unsigned)(s.swrAlarmTrig / 10), (unsigned)(s.swrAlarmTrig % 10),
             (unsigned)s.swrAlarmPwrThresh);
  addLine(body, b, UI_FONT_SMALL, UI_GREY);

  snprintf(b, sizeof(b), "Scale  %u / %u / %u per decade",
           (unsigned)s.scaleRange[0], (unsigned)s.scaleRange[1],
           (unsigned)s.scaleRange[2]);
  addLine(body, b, UI_FONT_SMALL, UI_GREY);

  snprintf(b, sizeof(b), "Touch  X %d..%d   Y %d..%d",
           (int)s.touchMinX, (int)s.touchMaxX,
           (int)s.touchMinY, (int)s.touchMaxY);
  addLine(body, b, UI_FONT_SMALL, UI_GREY);

  lv_obj_t* hint = lv_label_create(root_);
  lv_obj_set_style_text_font(hint, UI_FONT_SMALL, 0);
  lv_obj_set_style_text_color(hint, UI_DKGREY, 0);
  lv_label_set_text(hint, "Tap to return");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);
}

void AboutScreen::onTap(lv_event_t* e)
{
  LV_UNUSED(e);
  if (going_) return;         // the pop is deferred; do not queue it twice
  going_ = true;
  app_->requestPop();
}

//*********************************************************************************
//  DebugScreen.cpp
//*********************************************************************************
#include "DebugScreen.h"

#include <stdio.h>

#include "AppConfig.h"
#include "UiApp.h"
#include "UiCallback.h"
#include "UiFormat.h"
#include "UiTheme.h"

void DebugScreen::build()
{
  root_ = lv_obj_create(nullptr);
  UiTheme::styleScreen(root_);
  lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(root_, lvBind<DebugScreen, &DebugScreen::onTap>,
                      LV_EVENT_CLICKED, this);

  auto line = [&](const lv_font_t* font, lv_color_t colour, lv_coord_t y) {
    lv_obj_t* l = lv_label_create(root_);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, colour, 0);
    lv_obj_set_pos(l, 10, y);
    lv_label_set_text(l, "");
    return l;
  };

  lv_obj_t* title = line(UI_FONT_MED, UI_WHITE, 6);
  lv_label_set_text(title, "DEBUG - RAW DETECTOR VOLTAGES");

  lblVf_  = line(UI_FONT_LARGE, UI_GREEN, 36);
  lblVr_  = line(UI_FONT_LARGE, UI_PINK,  66);
  lblDet_ = line(UI_FONT_MED,   UI_CYAN,  102);
  lblPwr_ = line(UI_FONT_MED,   UI_CYAN,  122);
  //  Directly under the instantaneous figure it belongs with; the hardware line
  //  moved down to make room rather than the averages going below it.
  lblAvg_ = line(UI_FONT_MED,   UI_CYAN,  142);
  lblAds_ = line(UI_FONT_MED,   UI_GREEN, 168);

  lv_obj_t* hint = line(UI_FONT_MED, UI_DKGREY, 200);
  lv_label_set_text(hint, "Tap anywhere to return to menu");
}

void DebugScreen::onData(const MeterReadings& r, const Settings& s)
{
  if (++tick_ % TICKS_REFRESH) return;

  char b[48];

  snprintf(b, sizeof(b), "Vfwd: %.4f V", r.vFwd);
  lv_label_set_text(lblVf_, b);
  snprintf(b, sizeof(b), "Vrev: %.4f V", r.vRev);
  lv_label_set_text(lblVr_, b);

  if (s.detector == DetectorType::AD8307)
    snprintf(b, sizeof(b), "Fwd %.1f dBm   Rev %.1f dBm", r.ad8307FwdDbm, r.ad8307RevDbm);
  else
    snprintf(b, sizeof(b), "Coupling %.0f:1  Vdrop %.2f V  cal %.3f",
             BRIDGE_COUPLING, D_VDROP, s.activeCal().meterCal);
  lv_label_set_text(lblDet_, b);

  snprintf(b, sizeof(b), "Pwr %.1f mW   SWR %.2f", r.netPowerMw, r.swr);
  lv_label_set_text(lblPwr_, b);

  //  The two sliding averages PowerMath keeps.  They are shown HERE and only
  //  here on purpose: they cost 4.4 KB of RAM and two sums per sample, and
  //  before this line existed nothing in the firmware ever read either of them -
  //  the meter was carrying its single largest measurement buffer to produce a
  //  number no one could see.  Averaged power is also the figure the peak and
  //  PEP readouts on the meter face cannot give you: what the load is actually
  //  dissipating, rather than what the envelope touched.
  //
  //  UiFormat rather than the raw "%.1f mW" its neighbours use, because 1.1 kW
  //  prints as 1100000.0 and two of those do not fit the width - and because
  //  ranging them the same way the meter face does is what makes the two
  //  screens directly comparable, which is the point of showing them at all.
  char a100[16], a1s[16];
  UiFormat::powerMw(a100, sizeof(a100), r.avgPowerMw);
  UiFormat::powerMw(a1s,  sizeof(a1s),  r.avg1sPowerMw);
  snprintf(b, sizeof(b), "Avg100ms %s   Avg1s %s", a100, a1s);
  lv_label_set_text(lblAvg_, b);

  //  The live address, not a hardcoded 0x48: on a multi-coupler meter this is
  //  what says whether CPL2 really is the converter you think it is, and a
  //  coupler that never appears is almost always an ADDR strapping mistake.
  //
  //  couplerCount() alone cannot answer this any more - it is 2 with no
  //  hardware at all, because the mock simulates two couplers.  Only
  //  detectorPresent() separates a real bus from a simulated one.
  const uint8_t n    = app_->meter().couplerCount();
  const bool    real = app_->meter().detectorPresent();

  if (real)
    snprintf(b, sizeof(b), "CPL%u of %u   ADS1015 @ 0x%02X",
             (unsigned)s.coupler, (unsigned)n,
             (unsigned)app_->meter().activeAddress());
  else
    snprintf(b, sizeof(b), "CPL%u of %u   simulated - no ADS1015",
             (unsigned)s.coupler, (unsigned)n);

  lv_label_set_text(lblAds_, b);
  lv_obj_set_style_text_color(lblAds_, real ? UI_GREEN : UI_SIM, 0);
}

void DebugScreen::onTap(lv_event_t* e)
{
  LV_UNUSED(e);
  if (going_) return;
  going_ = true;
  app_->requestPop();
}

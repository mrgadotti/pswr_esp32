//*********************************************************************************
//  CalScreen.cpp
//*********************************************************************************
#include "CalScreen.h"

#include <math.h>
#include <stdio.h>

#include "AdjustScreen.h"
#include "AppConfig.h"
#include "UiApp.h"
#include "UiCallback.h"
#include "UiFormat.h"
#include "UiTheme.h"

//  Legacy button colours, converted from their RGB565 literals.
#define CAL_NAVY   lv_color_hex(0x00007B)   // C_NAVY  0x000F
#define CAL_GO     lv_color_hex(0x009500)   // 0x04A0
#define CAL_SET    lv_color_hex(0x633000)   // 0x6180
#define CAL_BACK   lv_color_hex(0x212021)   // 0x2104
#define CAL_RESET  lv_color_hex(0x840000)   // 0x8000

//  Minimum Fwd/Rev separation, in volts, for a calibration to be accepted.
static constexpr double CAL_QUAL_VOLTS = CAL_INP_QUALITY * LOGAMP_SLOPE * 0.001;

void CalScreen::build()
{
  const Settings& s = app_->config().settings();
  isAd8307_  = (s.activeCal().detector == DetectorType::AD8307);
  refDb10m_  = s.activeCal().calAd[0].db10m;

  root_ = lv_obj_create(nullptr);
  UiTheme::styleScreen(root_);

  const lv_coord_t w = lv_disp_get_hor_res(nullptr);

  auto line = [&](const lv_font_t* font, lv_color_t colour,
                  lv_coord_t x, lv_coord_t y, lv_coord_t lw,
                  lv_text_align_t align) {
    lv_obj_t* l = lv_label_create(root_);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, colour, 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, lw);
    lv_label_set_text(l, "");
    return l;
  };

  //  The coupler is named in the title because this screen writes into ONE
  //  coupler's calibration - the selected one.  Calibrating the wrong coupler
  //  produces readings that look right and are not, so it should be impossible
  //  to be here without knowing which.
  char t[48];
  snprintf(t, sizeof(t), "CALIBRATE CPL%u  -  %s", (unsigned)s.coupler,
           isAd8307_ ? "2x AD8307" : "Diode (1 point)");

  lv_obj_t* title = line(UI_FONT_MED, UI_WHITE, 0, 2, w, LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(title, t);

  lblVolts_ = line(UI_FONT_MED,   UI_WHITE,  8,  22, 200,    LV_TEXT_ALIGN_LEFT);
  lblState_ = line(UI_FONT_MED,   UI_GREEN,  w - 128, 22, 120, LV_TEXT_ALIGN_RIGHT);
  lblP1_    = line(UI_FONT_SMALL, UI_CYAN,   8,  44, w - 16, LV_TEXT_ALIGN_LEFT);
  lblP2_    = line(UI_FONT_SMALL, UI_CYAN,   8,  58, w - 16, LV_TEXT_ALIGN_LEFT);
  lblRef_   = line(UI_FONT_LARGE, UI_YELLOW, 0,  70, w,      LV_TEXT_ALIGN_CENTER);
  lblFlash_ = line(UI_FONT_MED,   UI_DKGREY, 0, 218, w,      LV_TEXT_ALIGN_CENTER);

  buildButtons(isAd8307_);

  flash(isAd8307_ ? "Apply carrier at Ref level, then SET"
                  : "Carrier into dummy load, set W, SET CAL", UI_DKGREY);
}

//*********************************************************************************
//  BUTTONS  -  geometry from the legacy showCalScreen()
//*********************************************************************************
void CalScreen::buildButtons(bool ad8307)
{
  const lv_coord_t w   = lv_disp_get_hor_res(nullptr);
  const lv_coord_t gap = 6, h = 36, y1 = 96, y2 = 140;

  auto makeBtn = [&](uint8_t i, lv_coord_t x, lv_coord_t y,
                     lv_coord_t bw, lv_coord_t bh,
                     lv_color_t colour, const char* label) {
    lv_obj_t* b = lv_btn_create(root_);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &UiTheme::styleBtn, 0);
    lv_obj_add_style(b, &UiTheme::styleBtnPressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, colour, 0);
    lv_obj_set_style_border_color(b, UI_WHITE, 0);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, bw, bh);
    lvSetIndex(b, i);
    lv_obj_add_event_cb(b, lvBind<CalScreen, &CalScreen::onButton>,
                        LV_EVENT_CLICKED, this);

    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_center(l);
  };

  //  Four steppers, always present.
  const lv_coord_t bw4 = (w - 5 * gap) / 4;
  const char* const STEP[4] = { "-10", "-1", "+1", "+10" };
  for (uint8_t i = 0; i < 4; i++)
    makeBtn(i, gap + i * (bw4 + gap), y1, bw4, h, CAL_NAVY, STEP[i]);

  if (ad8307) {
    const lv_coord_t bw3 = (w - 4 * gap) / 3;
    makeBtn(4, gap,               y2, bw3, h, CAL_GO,   "1-LEVEL");
    makeBtn(5, gap * 2 + bw3,     y2, bw3, h, CAL_SET,  "SET P1");
    makeBtn(6, gap * 3 + bw3 * 2, y2, bw3, h, CAL_SET,  "SET P2");
    makeBtn(7, gap, 184, w - 2 * gap, 32,     CAL_BACK, "BACK");
    buttonCount_ = 8;
  } else {
    //  Three buttons, not two: the coupler's turns ratio (N:1) is now a stored
    //  parameter of its own - see openBridgeCoupling() - rather than the fixed
    //  BRIDGE_COUPLING constant every coupler used to share.
    const lv_coord_t bw3 = (w - 4 * gap) / 3;
    makeBtn(4, gap,               y2, bw3, h, CAL_GO,   "SET CAL");
    makeBtn(5, gap * 2 + bw3,     y2, bw3, h, CAL_NAVY, "N:1");
    makeBtn(6, gap * 3 + bw3 * 2, y2, bw3, h, CAL_RESET, "RESET");
    makeBtn(7, gap, 184, w - 2 * gap, 32,     CAL_BACK, "BACK");
    buttonCount_ = 8;
  }
}

void CalScreen::flash(const char* msg, lv_color_t colour)
{
  lv_obj_set_style_text_color(lblFlash_, colour, 0);
  lv_label_set_text(lblFlash_, msg);
}

CalScreen::Quality CalScreen::quality(const MeterReadings& r) const
{
  if ((r.vFwd - r.vRev) > CAL_QUAL_VOLTS) return Fwd;
  if ((r.vRev - r.vFwd) > CAL_QUAL_VOLTS) return Rev;
  return Bad;
}

//*********************************************************************************
//  LIVE BLOCK
//*********************************************************************************
void CalScreen::onData(const MeterReadings& r, const Settings& s)
{
  if (++tick_ % TICKS_REFRESH) return;
  refreshLive(r, s);
}

void CalScreen::refreshLive(const MeterReadings& r, const Settings& s)
{
  const Calibration& c = s.activeCal();
  char b[48];

  snprintf(b, sizeof(b), "Vf: %.4f V   Vr: %.4f V", r.vFwd, r.vRev);
  lv_label_set_text(lblVolts_, b);

  if (isAd8307_)
  {
    switch (quality(r)) {
      case Fwd: lv_obj_set_style_text_color(lblState_, UI_GREEN,  0);
                lv_label_set_text(lblState_, "FWD OK");   break;
      case Rev: lv_obj_set_style_text_color(lblState_, UI_YELLOW, 0);
                lv_label_set_text(lblState_, "REV");      break;
      default:  lv_obj_set_style_text_color(lblState_, UI_RED,    0);
                lv_label_set_text(lblState_, "POOR sig"); break;
    }

    //  One level when the two channels share it, both when they do not.  A
    //  reverse-only calibration is the case that splits them, and it is exactly
    //  the case where a single number on screen would be a lie about one of the
    //  two fits - so the line grows rather than picking a channel to believe.
    UiFormat::calPoint(b, sizeof(b), "P1", c.calAd[0]);
    lv_label_set_text(lblP1_, b);
    UiFormat::calPoint(b, sizeof(b), "P2", c.calAd[1]);
    lv_label_set_text(lblP2_, b);

    snprintf(b, sizeof(b), "Ref: %+.1f dBm", refDb10m_ / 10.0);
    lv_label_set_text(lblRef_, b);
  }
  else
  {
    char num[24];
    UiFormat::powerMw(num, sizeof(num), r.fwdPowerMw);
    snprintf(b, sizeof(b), "Meas: %s", num);
    lv_obj_set_style_text_color(lblState_, UI_GREEN, 0);
    lv_label_set_text(lblState_, b);

    snprintf(b, sizeof(b), "meter_cal=%.4f  N=%.0f:1  Vdrop=%.2f",
             c.meterCal, c.bridgeCoupling, D_VDROP);
    lv_label_set_text(lblP1_, b);
    lv_label_set_text(lblP2_, "");

    snprintf(b, sizeof(b), "Known: %.0f W", refWatts_);
    lv_label_set_text(lblRef_, b);
  }
}

//*********************************************************************************
//  INPUT  -  ported 1:1 from the legacy handleCalBtn()
//*********************************************************************************
void CalScreen::onButton(lv_event_t* e)
{
  const uint8_t i = lvIndexOf(e);

  Settings&            s = app_->config().settings();
  const MeterReadings& r = app_->meter().readings();

  //  Everything stored below lands in the SELECTED coupler's calibration.
  Calibration& c = s.activeCal();

  //  Steppers.
  if (i < 4) {
    static const int16_t SDB[4] = { -100, -10, 10, 100 };
    static const float   SW[4]  = { -10, -1, 1, 10 };

    if (isAd8307_) {
      int32_t v = refDb10m_ + SDB[i];
      if (v < CAL_DBM_MIN) v = CAL_DBM_MIN;
      if (v > CAL_DBM_MAX) v = CAL_DBM_MAX;
      refDb10m_ = (int16_t)v;
    } else {
      refWatts_ += SW[i];
      if (refWatts_ < 1)    refWatts_ = 1;
      if (refWatts_ > 2000) refWatts_ = 2000;
    }
    refreshLive(r, s);
    return;
  }

  if (isAd8307_)
  {
    if (i == 7) { if (!going_) { going_ = true; app_->requestPop(); } return; }

    const Quality q = quality(r);
    if (q == Bad) { flash("Poor signal - nothing stored", UI_RED); return; }

    if (i == 4) {                                    // 1-LEVEL
      if (q == Fwd) {
        //  Forward dominant: the measured forward volts are copied into BOTH
        //  channels (the reverse detector is assumed identical), so both
        //  anchors move with them.
        c.calAd[0].db10m    = refDb10m_;
        c.calAd[0].revDb10m = refDb10m_;
        c.calAd[0].fwd      = r.vFwd;
        c.calAd[0].rev      = r.vFwd;
        c.calAd[1].db10m    = c.calAd[0].db10m    - 300;
        c.calAd[1].revDb10m = c.calAd[0].revDb10m - 300;
        c.calAd[1].fwd      = c.calAd[0].fwd - LOGAMP_SLOPE * 0.001 * 30;
        c.calAd[1].rev      = c.calAd[0].rev - LOGAMP_SLOPE * 0.001 * 30;
      } else {
        //  Coupler fed backwards: ONLY the reverse channel was measured, so
        //  only the reverse anchor moves.  db10m is deliberately not touched -
        //  it belongs to forward volts this press never looked at.
        c.calAd[0].revDb10m = refDb10m_;
        c.calAd[0].rev      = r.vRev;
        c.calAd[1].revDb10m = c.calAd[0].revDb10m - 300;
        c.calAd[1].rev      = c.calAd[0].rev - LOGAMP_SLOPE * 0.001 * 30;
      }
      app_->config().saveCal();
      flash(q == Fwd ? "1-Level stored (Fwd+Rev, P2 -30dB)"
                     : "1-Level stored (Rev, P2 -30dB)", UI_GREEN);
    }
    else if (i == 5 || i == 6) {                     // SET P1 / SET P2
      const uint8_t p = (i == 5) ? 0 : 1;

      //  THE ANCHOR TRAVELS WITH THE VOLTS IT WAS MEASURED AGAINST.  While the
      //  two channels shared one db10m, the `q == Rev` branch below - which
      //  writes `rev` and nothing else - still moved the forward fit's
      //  reference level, so the forward reading shifted by the difference
      //  between the two Ref levels without a single forward volt changing.
      if (q == Fwd) {
        //  Forward dominant: the reverse detector is assumed identical and gets
        //  the same volts, so it gets the same anchor too.
        c.calAd[p].db10m    = refDb10m_;
        c.calAd[p].revDb10m = refDb10m_;
        c.calAd[p].fwd      = r.vFwd;
        c.calAd[p].rev      = r.vFwd;
      } else {
        c.calAd[p].revDb10m = refDb10m_;
        c.calAd[p].rev      = r.vRev;
      }
      app_->config().saveCal();

      //  BOTH pairs, not just forward.  The reverse fit has its own slope and
      //  its own divide in determineDbm(), and the q == Rev path above writes
      //  rev WITHOUT touching fwd - so checking only fwd let the exact case
      //  this warning exists for through unreported.
      //
      //  Still stored, not rejected: SET P1 and SET P2 are separate presses, so
      //  the pair is legitimately degenerate between them and refusing here
      //  would make the two-press sequence impossible to complete.  The math
      //  protects itself - determineDbm() falls back to the nominal slope
      //  rather than dividing by a span under CAL_MIN_SPAN_V.
      const bool flatF = fabs(c.calAd[0].fwd - c.calAd[1].fwd) < CAL_MIN_SPAN_V;
      const bool flatR = fabs(c.calAd[0].rev - c.calAd[1].rev) < CAL_MIN_SPAN_V;

      if (flatF || flatR)
        flash(flatF ? (flatR ? "Stored - WARNING P1/P2 too close!"
                             : "Stored - WARNING Fwd P1/P2 too close!")
                    : "Stored - WARNING Rev P1/P2 too close!", UI_ORANGE);
      else
        flash(p == 0 ? "Point 1 stored" : "Point 2 stored", UI_GREEN);
    }
    refreshLive(r, s);
    return;
  }

  //  Diode.
  if (i == 7) { if (!going_) { going_ = true; app_->requestPop(); } return; }

  if (i == 4) {
    if (r.fwdPowerMw < 100.0) { flash("No carrier - apply power first", UI_RED); return; }
    float ratio = sqrt((refWatts_ * 1000.0) / r.fwdPowerMw);
    float nc    = c.meterCal * ratio;
    if (nc < 0.1)  nc = 0.1;
    if (nc > 10.0) nc = 10.0;
    c.meterCal = nc;
    app_->config().saveCal();

    char b[40];
    snprintf(b, sizeof(b), "Stored: meter_cal = %.4f", c.meterCal);
    flash(b, UI_GREEN);
  }
  else if (i == 5) {                                  // N:1
    //  Hands off to AdjustScreen rather than growing another stepper mode on
    //  this already-full button grid.  The turns ratio is an integer property
    //  of the hardware (10:1, 24:1, ...), not something dialled in against a
    //  live reading the way meter_cal is, so it does not need to share screen
    //  space with the live Vf/Vr block above.
    openBridgeCoupling();
    return;
  }
  else if (i == 6) {
    c.meterCal = 1.0;
    app_->config().saveCal();
    flash("meter_cal reset to 1.0000", UI_YELLOW);
  }

  refreshLive(r, s);
}

//*********************************************************************************
//  BRIDGE COUPLING  -  the transformer turns ratio (N:1), per coupler.
//
//  AD8307-only screens never reach here: the log amp reports dB directly and
//  its two-point fit already absorbs whatever the coupler's insertion loss is,
//  so there is no separate ratio for that front end to store.  It only applies
//  to the diode/Bruene math in PowerMath::addSample(), which multiplies the
//  detector volts by it before squaring - see Calibration::bridgeCoupling.
//*********************************************************************************
void CalScreen::openBridgeCoupling()
{
  Settings& s = app_->config().settings();

  int v = (int)lround(s.activeCal().bridgeCoupling);
  if (v < 1)   v = 1;
  if (v > 100) v = 100;

  //  Names the coupler for the same reason the title bar does: this still
  //  writes into the SELECTED coupler's calibration, and calibrating the
  //  wrong one produces a reading that looks right and is not.
  char t[24];
  snprintf(t, sizeof(t), "Coupling CPL%u", (unsigned)s.coupler);

  auto* scr = new AdjustScreen(t, "turns ratio (N:1)", v, 1, 100, 1,
                               /*tenths*/ false, /*topLbl*/ nullptr);
  scr->setOnDone(doneBridgeCoupling, this);
  app_->requestPush(scr);
}

void CalScreen::doneBridgeCoupling(void* ctx, int v)
{
  auto* self = static_cast<CalScreen*>(ctx);
  self->app_->config().settings().activeCal().bridgeCoupling = (double)v;
  self->app_->config().saveCal();
}

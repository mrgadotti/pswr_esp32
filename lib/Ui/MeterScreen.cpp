//*********************************************************************************
//  MeterScreen.cpp
//*********************************************************************************
#include "MeterScreen.h"

#include <stdio.h>
#include <string.h>

#include "AppConfig.h"
#include "MenuScreen.h"
#include "ModeIntroScreen.h"
#include "UiApp.h"
#include "UiCallback.h"
#include "UiTheme.h"

//  Legacy layout constants (DisplayManager.cpp), kept identical.
static constexpr lv_coord_t MX   = 9;
static constexpr lv_coord_t MLEN = 300;

//  Front panel.  The legacy row was 58 px of a 240 px panel - a quarter of the
//  display spent on four labels.  40 px still clears a fingertip on a resistive
//  panel (~7 mm here) and hands the 18 px back to the measurement area, which is
//  the part worth looking at.
static constexpr lv_coord_t FP_H   = 40;
static constexpr lv_coord_t FP_GAP = 4;

//  Panel size comes from LVGL, not from Ili9341Driver.h: the UI layer has no
//  business including a TFT_eSPI header just to learn how big the screen is.
static inline lv_coord_t scrW() { return lv_disp_get_hor_res(nullptr); }
static inline lv_coord_t scrH() { return lv_disp_get_ver_res(nullptr); }

//  Row anchored to the bottom edge, so the content area absorbs any change in
//  FP_H rather than the layout needing two constants kept in step.
static inline lv_coord_t fpY() { return scrH() - FP_H - 2; }

static const char* const PEP_LABELS[3] = { "100ms", "1s", "2.5s" };

//*********************************************************************************
//  ANCHOR SAMPLES  -  the widest string each right-hand readout can produce.
//
//  A readout anchored by its RIGHT edge slides sideways every time its text
//  gains a digit, so a power drifting 99.9W -> 100.5W appears to jump even
//  though the reading barely moved.  Anchoring by the LEFT edge instead - at the
//  x where the widest possible string would still end at MX + MLEN - holds the
//  first glyph still and lets the text grow into reserved space.
//
//  This costs NOTHING at runtime, which is the whole reason it is done this way.
//  The label stays LV_SIZE_CONTENT, so LVGL still invalidates only the glyphs.
//  Measured on hardware: giving these readouts fixed-width boxes instead took
//  the mode-2 frame from 9.5 ms to 10-22 ms, and splitting the unit into its own
//  right-pinned label took it to 12.5 ms - two disjoint invalid areas per
//  reading cost more in per-area overhead than they save in pixels.
//
//  They are contracts with UiFormat: powerMw() tops out at "999.9mW", dbm() at
//  "-99.9dBm", swr() at four characters.  Change a format and change these.
//*********************************************************************************
static const char* const W_POWER   = "999.9mW";
static const char* const W_DBM     = "-99.9dBm";
static const char* const W_SWR     = "SWR:9999";
static const char* const W_REF_W   = "Ref:999.9mW";
static const char* const W_PEP_W   = "PEP:999.9mW";
static const char* const W_PEP_DBM = "PEP:-99.9dBm";

//*********************************************************************************
//  BUILD
//*********************************************************************************
void MeterScreen::build()
{
  root_ = lv_obj_create(nullptr);
  UiTheme::styleScreen(root_);

  content_ = UiTheme::makePanel(root_);
  lv_obj_set_pos(content_, 0, 0);
  lv_obj_set_size(content_, scrW(), fpY() - 2);

  //  The whole measurement area acknowledges a latched SWR alarm - see the note
  //  in MeterScreen.h.  On the content panel rather than on the readouts because
  //  which labels exist depends on the mode, and an acknowledgement that moves
  //  with the display mode is one the operator has to hunt for.
  lv_obj_add_event_cb(content_, lvBind<MeterScreen, &MeterScreen::onAcknowledge>,
                      LV_EVENT_CLICKED, this);

  //  Empty for now: which buttons exist depends on the display mode, and that
  //  is not known until the first onData().
  fpRow_ = UiTheme::makePanel(root_);
  lv_obj_set_pos(fpRow_, 0, fpY());
  lv_obj_set_size(fpRow_, scrW(), FP_H);
}

//*********************************************************************************
//  FRONT PANEL
//
//  Rebuilt, not shown/hidden.  Hiding a button would leave its slot empty and
//  the remaining controls narrower than they need to be; rebuilding lets the row
//  divide the full width among however many controls this mode actually has.
//  Only ever called from onData(), so it is safe to delete objects here.
//*********************************************************************************
void MeterScreen::buildFrontPanel(DisplayMode mode)
{
  const bool pep = pepApplies(mode);
  if (fpBuilt_ && pep == fpHasPep_) return;      // same set - nothing to redo

  lv_obj_clean(fpRow_);
  for (uint8_t k = 0; k < FP_COUNT; k++) {
    fpBtn_[k] = fpLabel_[k] = nullptr;
    fpCache_[k][0] = 0;                          // force a relabel on next tick
  }

  //  One converter on the bus means one coupler to select, so the control is
  //  locked rather than offered: switching to a coupler that is not there would
  //  quietly apply an uncalibrated set of numbers to live readings.
  const bool cplLocked = (app_->meter().couplerCount() < 2);

  const uint8_t    n  = pep ? FP_COUNT : (FP_COUNT - 1);
  const lv_coord_t bw = (scrW() - (n + 1) * FP_GAP) / n;

  uint8_t slot = 0;
  for (uint8_t k = 0; k < FP_COUNT; k++)
  {
    if (k == FP_PEP && !pep) continue;

    lv_obj_t* b = lv_btn_create(fpRow_);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &UiTheme::styleBtn, 0);
    lv_obj_set_pos(b, FP_GAP + slot * (bw + FP_GAP), 0);
    lv_obj_set_size(b, bw, FP_H);

    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, "");
    lv_obj_center(l);

    if (k == FP_CPL && cplLocked) {
      //  Locked, not removed: the operator still has to be able to read WHICH
      //  coupler the meter is using.  LV_STATE_DISABLED is enough on its own -
      //  lv_obj_hit_test() rejects a disabled object before any event is sent -
      //  so there is no handler to guard.
      //
      //  The fill is what carries the message.  styleBtn already draws a dark
      //  grey border, so dimming the border would change nothing; dropping the
      //  raised face back to the canvas colour leaves an outlined label that no
      //  longer reads as a pressable control.  A shared style, not local
      //  properties, so a theme change reaches it - the row is only rebuilt
      //  when the visible button set changes, which a theme change is not.
      lv_obj_add_state(b, LV_STATE_DISABLED);
      lv_obj_add_style(b, &UiTheme::styleBtnLocked, 0);
    } else {
      //  Replaces the legacy delay(90) press flash: instant, and free.
      lv_obj_add_style(b, &UiTheme::styleBtnPressed, LV_STATE_PRESSED);
      lvSetIndex(b, k);
      lv_obj_add_event_cb(b, lvBind<MeterScreen, &MeterScreen::onFrontPanel>,
                          LV_EVENT_CLICKED, this);
    }

    fpBtn_[k]   = b;
    fpLabel_[k] = l;
    slot++;
  }

  fpHasPep_ = pep;
  fpBuilt_  = true;
}

//*********************************************************************************
//  MODE LAYOUTS  -  one per DisplayMode, geometry from the legacy mainScreenInit()
//*********************************************************************************
void MeterScreen::buildContent(DisplayMode mode)
{
  lv_obj_clean(content_);          // drops every child of the previous mode

  lblCaption_ = lblBig_ = lblRev_ = lblLeft_ = lblRight_ = nullptr;
  cacheBig_[0] = cacheRev_[0] = cacheLeft_[0] = cacheRight_[0] = 0;
  colCaption_ = colBig_ = colLeft_ = colRight_ = lv_color_black();

  //  EVERY label is sized to its CONTENT, never to a fixed box.
  //
  //  LVGL invalidates a label's whole object area when its text changes, so a
  //  readout given a 300 px box repaints 300 px of width even though the glyphs
  //  occupy a third of that.  Measured, the 48 px headline alone was ~180,000
  //  px/s at 10 Hz that way.
  //
  //  What `widest` changes is only the ANCHOR - see the note above the samples.
  //  Passing nullptr keeps the old behaviour: left-positioned at x, or
  //  right-aligned by lv_obj_align(), which re-applies the anchor whenever the
  //  size changes so the text still ends at MX + MLEN.
  const lv_coord_t rightInset = scrW() - (MX + MLEN);

  auto makeLabel = [&](const lv_font_t* font, lv_color_t colour,
                       lv_coord_t x, lv_coord_t y, const char* widest,
                       lv_text_align_t align) {
    lv_obj_t* l = lv_label_create(content_);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, colour, 0);
    lv_obj_set_width(l, LV_SIZE_CONTENT);

    if (widest) {
      lv_point_t sz;
      lv_txt_get_size(&sz, widest, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      lv_obj_set_pos(l, (align == LV_TEXT_ALIGN_RIGHT) ? (MX + MLEN - sz.x) : x, y);
    } else if (align == LV_TEXT_ALIGN_RIGHT) {
      lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -rightInset, y);
    } else {
      lv_obj_set_pos(l, x, y);
    }
    lv_label_set_text(l, "");
    return l;
  };

  switch (mode)
  {
    case DisplayMode::PowerMixed:
      powerBar_[0].create(content_, MX,  2, MLEN, 16);
      powerBar_[1].create(content_, MX, 36, MLEN, 16);
      swrBar_.create(     content_, MX, 70, MLEN, 14);
      lblCaption_ = makeLabel(UI_FONT_LARGE, UI_YELLOW, MX, 106, nullptr, LV_TEXT_ALIGN_LEFT);
      lblBig_     = makeLabel(UI_FONT_BIG,   UI_YELLOW, MX, 100, W_POWER, LV_TEXT_ALIGN_RIGHT);
      lblRev_     = makeLabel(UI_FONT_MED,   UI_RED,    MX, 134, nullptr, LV_TEXT_ALIGN_LEFT);
      lblLeft_    = makeLabel(UI_FONT_LARGE, UI_SWRTXT, MX, 150, nullptr, LV_TEXT_ALIGN_LEFT);
      lblRight_   = makeLabel(UI_FONT_LARGE, UI_YELLOW, MX, 150, W_REF_W, LV_TEXT_ALIGN_RIGHT);
      break;

    case DisplayMode::ModulationScope:
      scope_.create(content_, MX, 2, MLEN, 150);
      lblLeft_  = makeLabel(UI_FONT_LARGE, UI_YELLOW, MX, 156, nullptr, LV_TEXT_ALIGN_LEFT);
      lblRight_ = makeLabel(UI_FONT_LARGE, UI_GREEN,  MX, 156, W_SWR,   LV_TEXT_ALIGN_RIGHT);
      break;

    case DisplayMode::AnalogFwd:
      analogMeter_.create(content_, MX, 2, MLEN, 150);
      lblLeft_  = makeLabel(UI_FONT_LARGE, UI_YELLOW, MX, 156, nullptr, LV_TEXT_ALIGN_LEFT);
      lblRight_ = makeLabel(UI_FONT_LARGE, UI_SWRTXT, MX, 156, W_SWR,   LV_TEXT_ALIGN_RIGHT);
      break;

    case DisplayMode::PowerCleanDbm:
      powerBar_[0].create(content_, MX,  2, MLEN, 28);
      swrBar_.create(     content_, MX, 50, MLEN, 18);
      lblCaption_ = makeLabel(UI_FONT_LARGE, UI_YELLOW, MX, 92,  nullptr,   LV_TEXT_ALIGN_LEFT);
      lblBig_     = makeLabel(UI_FONT_LARGE, UI_YELLOW, MX, 120, W_DBM,     LV_TEXT_ALIGN_RIGHT);
      lblLeft_    = makeLabel(UI_FONT_LARGE, UI_SWRTXT, MX, 150, nullptr,   LV_TEXT_ALIGN_LEFT);
      lblRight_   = makeLabel(UI_FONT_LARGE, UI_YELLOW, MX, 150, W_PEP_DBM, LV_TEXT_ALIGN_RIGHT);
      break;

    default:   // PowerBarPep
      powerBar_[0].create(content_, MX,  2, MLEN, 28);
      swrBar_.create(     content_, MX, 50, MLEN, 18);
      lblCaption_ = makeLabel(UI_FONT_LARGE, UI_YELLOW, MX, 92,  nullptr, LV_TEXT_ALIGN_LEFT);
      lblBig_     = makeLabel(UI_FONT_BIG,   UI_YELLOW, MX, 86,  W_POWER, LV_TEXT_ALIGN_RIGHT);
      lblRev_     = makeLabel(UI_FONT_MED,   UI_RED,    MX, 120, nullptr, LV_TEXT_ALIGN_LEFT);
      lblLeft_    = makeLabel(UI_FONT_LARGE, UI_SWRTXT, MX, 150, nullptr, LV_TEXT_ALIGN_LEFT);
      lblRight_   = makeLabel(UI_FONT_LARGE, UI_YELLOW, MX, 150, W_PEP_W, LV_TEXT_ALIGN_RIGHT);
      break;
  }

  if (lblCaption_) {
    const char* cap = (mode == DisplayMode::PowerMixed)    ? "Fwd:"
                    : (mode == DisplayMode::PowerCleanDbm) ? "Power in dB:"
                                                           : "Pk:";
    lv_label_set_text(lblCaption_, cap);
  }

  builtMode_ = mode;
  haveMode_  = true;
}

void MeterScreen::onEnter()
{
  haveMode_ = false;      // force a rebuild on the next onData()
  tick_     = 0;
}

//*********************************************************************************
//  DATA
//*********************************************************************************
void MeterScreen::setText(lv_obj_t* label, char* cache, size_t cacheN, const char* txt)
{
  if (!label) return;
  if (strncmp(cache, txt, cacheN) == 0) return;
  strncpy(cache, txt, cacheN - 1);
  cache[cacheN - 1] = 0;
  lv_label_set_text(label, txt);
}

void MeterScreen::setColor(lv_obj_t* label, lv_color_t& cache, lv_color_t c)
{
  if (!label) return;
  //  lv_color_eq() is LVGL 9; in 8.x compare the packed value.
  if (cache.full == c.full) return;
  cache = c;
  lv_obj_set_style_text_color(label, c, 0);
}

void MeterScreen::onData(const MeterReadings& r, const Settings& s)
{
  if (!haveMode_ || s.modeDisplay != builtMode_) {
    buildContent(s.modeDisplay);
    buildFrontPanel(s.modeDisplay);
  }

  //  The scope's column rate is this tick rate - one per call, no division.
  if (builtMode_ == DisplayMode::ModulationScope) {
    double fs = autoScale_.push(r.pepPowerMw, s);
    scope_.add(r.netPowerMw, fs);
  }

  tick_++;
  if (tick_ % TICKS_BARS == 0) updateBars(r, s);
  if (tick_ % TICKS_TEXT == 0) { updateText(r, s); refreshFrontPanel(s); }
}

void MeterScreen::updateBars(const MeterReadings& r, const Settings& s)
{
  if (builtMode_ == DisplayMode::ModulationScope) return;

  //  The gauge has no SWR bar - see the AnalogFwd case in buildContent() - so
  //  it stops here too, same as ModulationScope above.  fwdPowerMw, not
  //  peakPowerMw: the needle is the forward-power figure this mode exists to
  //  show, autoscaled exactly like PowerMixed's bars are.
  if (builtMode_ == DisplayMode::AnalogFwd) {
    double adj;
    char   unit[6];
    const double scale = autoScale_.push(r.fwdPowerMw, s);
    UiFormat::scalePowerMeter(scale, &adj, unit);
    analogMeter_.setScale(adj, unit);
    analogMeter_.setValue(r.fwdPowerMw, scale, r.swrAlarm);
    return;
  }

  const double alarm = (s.swrAlarmTrig != 40) ? s.swrAlarmTrig / 10.0 : 10.0;
  const double mid   = (alarm < 2.0) ? alarm : 2.0;

  double adj;
  char   unit[6];

  if (builtMode_ == DisplayMode::PowerMixed)
  {
    const double scale = autoScale_.push(r.fwdPowerMw, s);
    UiFormat::scalePowerMeter(scale, &adj, unit);
    powerBar_[0].setScale(adj, unit);
    powerBar_[0].setValues(r.fwdPowerMw, 0, scale);
    powerBar_[1].setScale(adj, unit);
    powerBar_[1].setValues(r.refPowerMw, 0, scale);
  }
  else
  {
    const double barMw = (builtMode_ == DisplayMode::PowerCleanDbm) ? r.netPowerMw
                                                                    : r.peakPowerMw;
    const double scale = autoScale_.push(r.pepPowerMw, s);
    UiFormat::scalePowerMeter(scale, &adj, unit);
    powerBar_[0].setScale(adj, unit);
    powerBar_[0].setValues(barMw, r.pepPowerMw, scale);
  }

  swrBar_.setValues(mid, alarm, r.swrAvg);
}

void MeterScreen::updateText(const MeterReadings& r, const Settings& s)
{
  char num[24], buf[32];

  //  Orange caption is the legacy signal for "no ADS1015, running on the mock".
  setColor(lblCaption_, colCaption_, app_->meter().detectorPresent() ? UI_YELLOW : UI_SIM);
  setColor(lblBig_,     colBig_,     r.swrAlarm ? UI_RED : UI_READING);

  UiFormat::swr(num, sizeof(num), r.swrAvg);
  snprintf(buf, sizeof(buf), "SWR:%s", num);

  switch (builtMode_)
  {
    case DisplayMode::ModulationScope:
      setColor(lblRight_, colRight_, r.swrAlarm ? UI_RED : UI_GREEN);
      setText(lblRight_, cacheRight_, sizeof(cacheRight_), buf);
      UiFormat::powerMw(num, sizeof(num), r.pepPowerMw);
      snprintf(buf, sizeof(buf), "PEP:%s", num);
      setText(lblLeft_, cacheLeft_, sizeof(cacheLeft_), buf);
      return;

    case DisplayMode::AnalogFwd:
      setColor(lblRight_, colRight_, r.swrAlarm ? UI_RED : UI_SWRTXT);
      setText(lblRight_, cacheRight_, sizeof(cacheRight_), buf);
      UiFormat::powerMw(num, sizeof(num), r.fwdPowerMw);
      snprintf(buf, sizeof(buf), "Fwd:%s", num);
      setText(lblLeft_, cacheLeft_, sizeof(cacheLeft_), buf);
      return;

    case DisplayMode::PowerCleanDbm:
      UiFormat::dbm(num, sizeof(num), (int16_t)(10 * r.peakPowerDb));
      setText(lblBig_, cacheBig_, sizeof(cacheBig_), num);

      setColor(lblLeft_, colLeft_, r.swrAlarm ? UI_RED : UI_SWRTXT);
      setText(lblLeft_, cacheLeft_, sizeof(cacheLeft_), buf);

      UiFormat::dbm(num, sizeof(num), (int16_t)(10 * r.pepPowerDb));
      snprintf(buf, sizeof(buf), "PEP:%s", num);
      setText(lblRight_, cacheRight_, sizeof(cacheRight_), buf);
      return;

    default:
    {
      const bool mixed = (builtMode_ == DisplayMode::PowerMixed);

      UiFormat::powerMw(num, sizeof(num), mixed ? r.fwdPowerMw : r.peakPowerMw);
      setText(lblBig_, cacheBig_, sizeof(cacheBig_), num);

      setText(lblRev_, cacheRev_, sizeof(cacheRev_), r.reverse ? "REV" : "");

      setColor(lblLeft_, colLeft_, r.swrAlarm ? UI_RED : UI_SWRTXT);
      setText(lblLeft_, cacheLeft_, sizeof(cacheLeft_), buf);

      if (mixed) {
        UiFormat::powerMw(num, sizeof(num), r.refPowerMw);
        snprintf(buf, sizeof(buf), "Ref:%s", num);
      } else {
        UiFormat::powerMw(num, sizeof(num), r.pepPowerMw);
        snprintf(buf, sizeof(buf), "PEP:%s", num);
      }
      setText(lblRight_, cacheRight_, sizeof(cacheRight_), buf);
      return;
    }
  }
}

void MeterScreen::refreshFrontPanel(const Settings& s)
{
  char b[10];
  for (uint8_t k = 0; k < FP_COUNT; k++)
  {
    if (!fpLabel_[k]) continue;                  // not on this mode's row

    switch (k) {
      case FP_MODE: snprintf(b, sizeof(b), "MODE"); break;
      case FP_CPL:  snprintf(b, sizeof(b), "CPL%u", (unsigned)s.coupler); break;
      case FP_PEP:  snprintf(b, sizeof(b), "%s", PEP_LABELS[s.pepIdx % 3]); break;
      default:      snprintf(b, sizeof(b), "SET");  break;
    }
    setText(fpLabel_[k], fpCache_[k], sizeof(fpCache_[k]), b);
  }
}

//*********************************************************************************
//  INPUT
//
//  Runs inside LVGL's event dispatch, so it only ever MUTATES STATE.  The mode
//  change is picked up and the widgets rebuilt by the next onData(); the NVS
//  write is debounced by UiApp.  Nothing here deletes or creates an object.
//*********************************************************************************

//  A tap anywhere on the measurement area.  Only ever an acknowledgement, so
//  with no alarm latched it does nothing at all and a stray touch on the meter
//  face costs nothing.  The clear happens on core 0; the red readouts and the
//  LED follow on the next frame.
void MeterScreen::onAcknowledge(lv_event_t*)
{
  app_->meter().clearSwrAlarm();
}

void MeterScreen::onFrontPanel(lv_event_t* e)
{
  Settings& s = app_->config().settings();

  switch (lvIndexOf(e))
  {
    case FP_MODE: {
      uint8_t m = (uint8_t)s.modeDisplay + 1;
      if (m > DISPLAY_MODE_MAX) m = DISPLAY_MODE_MIN;
      s.modeDisplay = (DisplayMode)m;
      app_->markSettingsDirty();
      //  The banner sits on TOP of the meter, so returning is a pop and this
      //  screen rebuilds itself for the new mode in its own onEnter().
      app_->requestPush(new ModeIntroScreen(s.modeDisplay));
      return;
    }

    //  Cycle only through the couplers that answered on the bus.  The meter
    //  task on core 0 notices this byte changing and repoints its detector, so
    //  nothing here has to reach across cores.  (Unreachable when only one is
    //  fitted: the button is disabled and sends no events.)
    case FP_CPL: {
      const uint8_t n = app_->meter().couplerCount();
      s.coupler = (uint8_t)((s.coupler % (n ? n : 1)) + 1);
      break;
    }

    case FP_PEP: s.pepIdx = (s.pepIdx + 1) % 3; break;

    default:
      //  SET opens the config menu.  Deferred, like every other navigation:
      //  building a screen here would run inside LVGL's event dispatch.
      app_->requestPush(new MenuScreen());
      return;
  }

  app_->markSettingsDirty();
  //  Force the label row to refresh on the next data tick rather than waiting
  //  for the 10-tick text cadence.
  tick_ = TICKS_TEXT - 1;
}

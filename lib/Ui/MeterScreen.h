//*********************************************************************************
//  MeterScreen.h  -  The live meter: four display modes plus the front panel.
//
//  Layout geometry is carried over verbatim from the legacy DisplayManager so
//  the port can be compared against the old firmware pixel by pixel.
//
//  REFRESH RATES.  The legacy UI alternated graphs and text on successive ticks
//  (the `alt_` flag), giving both ~50 Hz.  Here they are decoupled and the text
//  deliberately drops to 10 Hz: the numbers are not readable at 50 Hz, and each
//  text redraw is a ~150x26 px flush.  That removes most of this screen's SPI
//  traffic for no perceptual loss.
//
//      scope   every tick        (100 Hz - the sweep rate IS the tick rate)
//      bars    every 2nd tick    (50 Hz, as before)
//      text    every 10th tick   (10 Hz, was 50 Hz)
//
//  MODE CHANGES are applied in onData(), never in the button handler.  Tearing
//  down and rebuilding widgets from inside an LVGL event callback would free
//  objects the event dispatcher is still walking.
//
//  THE FRONT PANEL only carries controls that do something HERE AND NOW:
//
//      MODE  cycles the display mode
//      CPL   selects the coupler, and is locked when only one is fitted
//      PEP   the 100ms / 1s / 2.5s envelope window, shown only in the modes
//            that read the PEP figure at all
//      SET   opens the config menu
//
//  A control that is present but inert is worse than one that is absent: it
//  invites the operator to press it and conclude the meter is broken.  The PEP
//  window is genuinely ignored in Fwd/Ref mode - nothing there reads pepPower -
//  so the button leaves rather than sitting there doing nothing, and the row
//  reflows to fill the gap.
//
//  ALARM ACKNOWLEDGEMENT: a tap anywhere on the measurement area clears a
//  latched SWR alarm.  The latch is the point - a mismatch that flashed by
//  during a tune-up must still be visible afterwards - but a latch with no way
//  out is not a latch, it is a meter that is permanently red after one bad
//  moment, which is where this started.  There is no button because the alarm
//  is already shouting: the readouts are red and the LED is lit, so the surface
//  the operator is looking at IS the affordance, and a fifth front-panel control
//  that is inert whenever nothing is wrong would violate the rule just above.
//*********************************************************************************
#pragma once

#include "AnalogMeterWidget.h"
#include "PowerBarWidget.h"
#include "Screen.h"
#include "ScopeWidget.h"
#include "SwrBarWidget.h"
#include "UiFormat.h"

class MeterScreen : public Screen {
public:
  void build() override;
  void onEnter() override;
  void onData(const MeterReadings& r, const Settings& s) override;

private:
  //  Front-panel controls.  The value stashed on a button is its KEY, not its
  //  slot, so hiding one reflows the row without renumbering the handler.
  enum FpKey : uint8_t { FP_MODE = 0, FP_CPL, FP_PEP, FP_SET, FP_COUNT };

  //  Whether the PEP envelope window changes anything in this mode.  Fwd/Ref
  //  reads fwdPowerMw and refPowerMw and autoscales on the forward figure, so
  //  the PEP window is not consulted anywhere on that screen - and neither is
  //  it on the analog gauge, which reads fwdPowerMw the same way.
  static bool pepApplies(DisplayMode m) {
    return m != DisplayMode::PowerMixed && m != DisplayMode::AnalogFwd;
  }

  void buildContent(DisplayMode mode);
  void buildFrontPanel(DisplayMode mode);
  void refreshFrontPanel(const Settings& s);
  void updateBars(const MeterReadings& r, const Settings& s);
  void updateText(const MeterReadings& r, const Settings& s);

  void onFrontPanel(lv_event_t* e);
  void onAcknowledge(lv_event_t* e);

  //  Text setter with the guard LVGL 8 does not do for you: lv_label_set_text()
  //  reallocs, copies and invalidates unconditionally, even for an identical
  //  string.  Without this, an idle meter screen costs ~150 KB/s of SPI.
  void setText(lv_obj_t* label, char* cache, size_t cacheN, const char* txt);

  //  Same story for colours: lv_obj_set_style_text_color() invalidates the
  //  object whether or not the colour changed.  Only three labels ever change
  //  colour (alarm state, mock state), so cache those three.
  void setColor(lv_obj_t* label, lv_color_t& cache, lv_color_t c);

  static constexpr uint8_t TICKS_BARS = 2;
  static constexpr uint8_t TICKS_TEXT = 10;

  lv_obj_t* content_ = nullptr;      // everything above the front panel
  lv_obj_t* fpRow_   = nullptr;

  //  Indexed by FpKey; a hidden control leaves its slot null and every writer
  //  checks (setText() already does).
  lv_obj_t* fpBtn_[FP_COUNT]   = { nullptr };
  lv_obj_t* fpLabel_[FP_COUNT] = { nullptr };
  char      fpCache_[FP_COUNT][10] = { { 0 } };

  //  The row is rebuilt only when the visible SET changes, not on every mode
  //  change: a rebuild deletes and recreates the buttons, and doing that when
  //  the outcome is identical would flash the row for nothing.
  bool fpHasPep_ = false;
  bool fpBuilt_  = false;

  PowerBarWidget    powerBar_[2];
  SwrBarWidget      swrBar_;
  ScopeWidget       scope_;
  AnalogMeterWidget analogMeter_;

  //  Text slots.  Which of these exist depends on the mode; unused ones stay
  //  null and every writer checks.
  lv_obj_t* lblCaption_ = nullptr;   // "Pk:" / "Fwd:" / "Power in dB:"
  lv_obj_t* lblBig_     = nullptr;   // the headline reading
  lv_obj_t* lblRev_     = nullptr;   // "REV"
  lv_obj_t* lblLeft_    = nullptr;   // "SWR:..."
  lv_obj_t* lblRight_   = nullptr;   // "PEP:..." / "Ref:..."

  lv_color_t colCaption_ = lv_color_black();
  lv_color_t colBig_     = lv_color_black();
  lv_color_t colLeft_    = lv_color_black();
  lv_color_t colRight_   = lv_color_black();

  char cacheBig_[24]   = { 0 };
  char cacheRev_[8]    = { 0 };
  char cacheLeft_[24]  = { 0 };
  char cacheRight_[24] = { 0 };

  UiFormat::AutoScale autoScale_;

  DisplayMode builtMode_ = DisplayMode::PowerBarPep;
  bool        haveMode_  = false;
  uint32_t    tick_      = 0;
};

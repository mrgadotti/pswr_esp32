//*********************************************************************************
//  UiTheme.h  -  One place for every colour, font and shared style.
//
//  COLOURS.  The legacy UI used raw RGB565 literals (C_GREY 0x8410 and friends).
//  Those cannot survive the port as-is: with LV_COLOR_16_SWAP = 1 LVGL stores
//  colours byte-swapped, so a hand-rolled 565 literal comes out wrong.  Every
//  colour is therefore respecified in RGB888 and converted by lv_color_hex().
//
//  THE PALETTE IS RUNTIME STATE, not constants: the UI_* names below expand to
//  variables that applyTheme() rewrites.  Every existing call site is unchanged
//  by that, which is the whole reason the names were introduced in the first
//  place.  Two consequences worth knowing:
//
//    - The names are ROLES, not literals.  UI_BLACK is "the canvas" and UI_WHITE
//      is "the ink"; in the light theme they hold white and near-black.  Code
//      that wants a literal black regardless of theme must not use UI_BLACK.
//    - Widgets that draw in an LV_EVENT_DRAW_MAIN handler read the palette at
//      draw time, so the bars and the scope follow a theme change for free.
//      Anything that bakes a colour into an object at build time does not - see
//      applyTheme() for what is done about that.
//
//  FONTS.  Deliberately behind aliases rather than used directly.  Montserrat
//  renders visually smaller than the TFT_eSPI bitmap fonts at the same nominal
//  size (cap height ~0.70 em against ~0.75), so the mapping below is a starting
//  point that WILL need bench tuning - and the point of the alias layer is that
//  tuning it is a one-line change here instead of a hunt through seven screens.
//
//      legacy TFT font 1 (GLCD 8 px)   -> UI_FONT_TICK / UI_FONT_SMALL
//      legacy TFT font 2 (16 px)       -> UI_FONT_MED
//      legacy TFT font 4 (26 px)       -> UI_FONT_LARGE
//      legacy font 4 + setTextSize(2)  -> UI_FONT_BIG
//      legacy TFT font 6 (48 px)       -> UI_FONT_BIG
//*********************************************************************************
#pragma once

#include <lvgl.h>

#include "Types.h"

namespace UiTheme {

//  ---- the live palette ------------------------------------------------------
//  Role names, in the same order as the macros below.  Never assign to these
//  directly; applyTheme() owns them, because changing a colour without also
//  refreshing the shared styles leaves half the screen on the old theme.
extern lv_color_t cBlack;    // canvas / background
extern lv_color_t cWhite;    // ink / primary text
extern lv_color_t cGrey;     // secondary text
extern lv_color_t cDkGrey;   // dim text, hints, borders
extern lv_color_t cGreen;
extern lv_color_t cRed;
extern lv_color_t cYellow;   // the headline accent (TEXT: captions, titles)
extern lv_color_t cOrange;   // warning
extern lv_color_t cSim;      // "these readings are simulated"

//  The meter's two headline texts, split out of cYellow/cWhite so a theme can
//  colour them the way an editor colours a keyword and its value.  On the older
//  themes they hold exactly what those two roles held before, so nothing moves;
//  on Dark 2026 the caption is the keyword purple, the reading is plain white
//  and the SWR is the type teal.
extern lv_color_t cReading;  // the big number
extern lv_color_t cSwrTxt;   // the "SWR:x.xx" line

//  Bargraph, kept out of the text roles on purpose.  A colour used as text on a
//  pale canvas has to be dark; the same colour used as a 300 px block of fill
//  has to be saturated, and on the light theme those two are not the same
//  colour.  Splitting them is what lets the bar stay vivid yellow in both themes
//  while the headline reading goes blue on light.
extern lv_color_t cBarLive;    // the live power segment
extern lv_color_t cBarTrough;  // the unfilled remainder, on both bars
extern lv_color_t cCyan;
extern lv_color_t cPink;
extern lv_color_t cLtBlue;
extern lv_color_t cBtnBg;    // raised button face
extern lv_color_t cBtnAct;   // button, pressed
extern lv_color_t cMenuHi;       // selected list row background
extern lv_color_t cMenuHiText;   // ...and its text, stated per theme

}  // namespace UiTheme

//  ---- palette macros (unchanged names; the comment is the RGB565 literal the
//       dark theme replaces) ------------------------------------------------
#define UI_BLACK   UiTheme::cBlack     // C_BLACK
#define UI_WHITE   UiTheme::cWhite     // C_WHITE
#define UI_GREY    UiTheme::cGrey      // C_GREY   0x8410
#define UI_DKGREY  UiTheme::cDkGrey    // C_DKGREY 0x4208
#define UI_GREEN   UiTheme::cGreen     // C_GREEN
#define UI_RED     UiTheme::cRed       // C_RED
#define UI_YELLOW  UiTheme::cYellow    // C_YELLOW
#define UI_ORANGE  UiTheme::cOrange    // C_ORANGE 0xFDA0
#define UI_SIM     UiTheme::cSim
#define UI_READING UiTheme::cReading
#define UI_SWRTXT  UiTheme::cSwrTxt

#define UI_BAR_LIVE    UiTheme::cBarLive
#define UI_BAR_TROUGH  UiTheme::cBarTrough
#define UI_CYAN    UiTheme::cCyan      // C_CYAN
#define UI_PINK    UiTheme::cPink      // C_PINK   0xFE19
#define UI_LTBLUE  UiTheme::cLtBlue    // C_LTBLUE 0x06FF
#define UI_BTNBG   UiTheme::cBtnBg     // C_BTNBG  0x20C3
#define UI_BTNACT  UiTheme::cBtnAct    // C_BTNACT 0x12A0
#define UI_MENUHI  UiTheme::cMenuHi    // C_MENUHI 0xB6DF

//  ---- font aliases ----------------------------------------------------------
#define UI_FONT_TICK   (&lv_font_montserrat_10)
#define UI_FONT_SMALL  (&lv_font_montserrat_12)
#define UI_FONT_MED    (&lv_font_montserrat_14)
#define UI_FONT_LARGE  (&lv_font_montserrat_20)
#define UI_FONT_BIG    (&lv_font_montserrat_48)

namespace UiTheme {

//  Initialise the shared styles and select a theme.  Call once, after lv_init().
void begin(ThemeMode theme);

//  Switch the palette at runtime.  Rewrites the palette variables, re-fills
//  every shared style from them, and reports the change to LVGL so that objects
//  built from those styles repaint.
//
//  What this CANNOT reach is a colour a screen applied to one object at build
//  time.  Those are handled by the screens themselves being short-lived: every
//  screen except the meter is rebuilt on entry, and the meter rebuilds its
//  contents in onEnter().  Anything long-lived is styled from the shared styles
//  below precisely so that this function can reach it.
void applyTheme(ThemeMode theme);

//  Panel-style pushbutton: rounded, flat pressed state.  The pressed style is
//  what replaces every delay(90) press-flash in the legacy UI.
extern lv_style_t styleBtn;
extern lv_style_t styleBtnPressed;

//  A control that is present but not operable - the coupler selector on a meter
//  with a single ADS1015.  Drops the raised face so it stops reading as
//  pressable while staying legible.
extern lv_style_t styleBtnLocked;

//  Bare container: opaque black, no padding, no border, not scrollable.
//  (Every lv_obj_create() container is scrollable by default in LVGL 8 - that
//  default swallows drags and adds scrollbars where none were wanted.)
extern lv_style_t stylePanel;

//  Screen background.  A shared style rather than local properties so that a
//  theme change repaints screens that are already built.
extern lv_style_t styleScreenBg;

//  Screen heading, for the same reason.
extern lv_style_t styleTitle;

//  1 px white frame used by the bar widgets and the scope.
extern lv_style_t styleFrame;

//  Menu list row, and its selected state.  Selection is LV_STATE_CHECKED rather
//  than a hand-drawn highlight bar, so button-select and touch-select share one
//  visual state instead of two code paths.
extern lv_style_t styleRow;
extern lv_style_t styleRowSelected;

//  Apply the screen-level background so a screen never inherits the theme's.
void styleScreen(lv_obj_t* scr);

//  Make an opaque, non-scrollable, zero-padding container child.
lv_obj_t* makePanel(lv_obj_t* parent);

}  // namespace UiTheme

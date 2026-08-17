//*********************************************************************************
//  UiTheme.cpp
//*********************************************************************************
#include "UiTheme.h"

namespace UiTheme {

static void refreshStyles();

lv_color_t cBlack, cWhite, cGrey, cDkGrey, cGreen, cRed, cYellow,
           cOrange, cCyan, cPink, cLtBlue, cBtnBg, cBtnAct, cMenuHi, cMenuHiText,
           cSim, cBarLive, cBarTrough, cReading, cSwrTxt;

lv_style_t styleBtn;
lv_style_t styleBtnPressed;
lv_style_t styleBtnLocked;
lv_style_t stylePanel;
lv_style_t styleScreenBg;
lv_style_t styleTitle;
lv_style_t styleFrame;
lv_style_t styleRow;
lv_style_t styleRowSelected;

//*********************************************************************************
//  PALETTES
//
//  The light theme is NOT an inversion.  Swapping canvas and ink is the easy
//  half; the accents have to be re-picked, because the saturated colours that
//  carry a dark UI (pure yellow, pure green, pure cyan) have almost no contrast
//  against white and would make the headline reading - the one number the whole
//  instrument exists to show - the hardest thing on the screen to read.
//
//  Most accents keep their HUE, which is what carries the meaning (red = alarm,
//  green = good, orange = simulated), and trade brightness for depth.  The one
//  that does not is the primary accent - see the note on yellow below.
//*********************************************************************************
struct Palette {
  uint32_t black, white, grey, dkGrey, green, red, yellow,
           orange, cyan, pink, ltBlue, btnBg, btnAct, menuHi, menuHiText,
           sim, barLive, barTrough, reading, swrTxt;
};

static const Palette DARK = {
  /* black  */ 0x000000, /* white  */ 0xFFFFFF,
  /* grey   */ 0x808080, /* dkGrey */ 0x404040,
  /* green  */ 0x00FF00, /* red    */ 0xFF0000,
  /* yellow */ 0xFFFF00, /* orange */ 0xFFB600,
  /* cyan   */ 0x00FFFF, /* pink   */ 0xFFC2CD,
  /* ltBlue */ 0x00DEFF,
  /* btnBg  */ 0x211819, /* btnAct */ 0x105500,
  /* menuHi */ 0xB5DAFF, /* menuHiText */ 0x000000,
  /* sim    */ 0xFFB600,
  /* barLive*/ 0xFFFF00, /* barTrough */ 0x000000,
  /* reading*/ 0xFFFF00, /* swrTxt    */ 0xFFFFFF,
};

static const Palette LIGHT = {
  //  Plain white.  An off-white canvas is the usual advice - it takes the glare
  //  off a bright panel - but this panel is already dimmable, so the brightness
  //  control does that job better than a grey background can, and without
  //  costing every other colour some contrast.  Ink stays near-black: 18.7:1.
  /* black  */ 0xFFFFFF, /* white  */ 0x121212,

  //  Secondary text is the SAME black as primary.  A grey that is legible on
  //  black is not automatically legible on off-white, and the in-between tones
  //  that survive the swap read as washed out rather than as a second tier -
  //  so on this theme the hierarchy is carried by size and position, not by
  //  fading the text.  (dkGrey is untouched: it is hints AND the button border,
  //  which is a different job.)
  /* grey   */ 0x121212, /* dkGrey */ 0x9A9A9A,

  /* green  */ 0x006B14, /* red    */ 0xB00000,

  //  The TEXT accent moves from amber to blue.  Amber is what carries the dark
  //  theme, but it has no useful range on a pale canvas: dark enough to read as
  //  text and it is mud, bright enough to be amber and it is invisible.
  //
  //  This is LVGL's own lv_palette_darken(LV_PALETTE_BLUE, 3) - written as a
  //  literal so the whole palette stays one readable table, but taken from there
  //  rather than invented.  5.8:1, against 3.6:1 for the lighter blues in that
  //  same ramp, which matters because the accent carries the headline reading.
  /* yellow */ 0x1565C0, /* orange */ 0xA34F00,

  /* cyan   */ 0x006E7A, /* pink   */ 0xA0203C,
  /* ltBlue */ 0x0057A8,
  /* btnBg  */ 0xE2E2DE, /* btnAct */ 0xA8D8A0,

  //  A wash, not a solid block, because the selected row keeps the SAME text
  //  colour as every other row here - selection is shown by the band behind the
  //  text, not by recolouring it.  Which means the band is now the ONLY signal,
  //  so it cannot be as faint as it looks like it wants to be: this sits at
  //  1.9:1 against the canvas (a lighter tint measured 1.3:1, which is a
  //  selection you have to hunt for) while still leaving 9.9:1 for the text.
  /* menuHi */ 0x99C0E8, /* menuHiText */ 0x121212,

  //  "Simulated" is the accent blue here, not the warning amber: on a meter
  //  with no ADS1015 fitted this marker is on permanently, and a permanent
  //  warning colour is just noise.  The dark theme keeps amber, where it is
  //  legible and where the marker is the only amber that is not the reading.
  /* sim    */ 0x1565C0,

  //  Vivid yellow, as on dark - and it only survives here because the trough
  //  stops being the canvas.  0xFFFF00 against a white background is 1.07:1,
  //  i.e. invisible; against this grey it is 2.1:1, and against the green PEP
  //  tail beside it, 6.3:1.  The trough IS the fix - move it back to the canvas
  //  colour and the bar disappears again.
  /* barLive*/ 0xFFFF00, /* barTrough */ 0xA6ADB4,

  //  Unchanged roles, stated explicitly: the reading was already the accent and
  //  the SWR line was already the ink.
  /* reading*/ 0x1565C0, /* swrTxt    */ 0x121212,
};

//*********************************************************************************
//  DARK 2026  -  VS Code's current default dark, on a BLACK canvas.
//
//  Two sources, both from the editor: the CHROME comes from Dark Modern (the
//  default since 1.75) and the TEXT comes from the Dark+ token colours - the
//  ones you actually look at all day.  That is the point of this theme: the
//  meter is coloured the way the editor colours code.
//
//      caption "Fwd:" / "Pk:"   #C586C0   control keyword
//      the reading              #FFFFFF   plain, the value is the value
//      "SWR:x.xx"               #4EC9B0   type / class
//      secondary text           #CCCCCC   editor foreground
//      hints and borders        #9D9D9D   description foreground
//
//  The canvas is BLACK rather than VS Code's #1F1F1F: an editor's dark grey
//  exists to keep a bright page from glaring, and a meter with a dimmable
//  backlight does not need it.  Dropping to black buys every colour ~28% more
//  contrast without touching a single hue - the keyword purple goes 5.9 ->
//  7.6:1, the teal 8.1 -> 10.3, and the reading itself 16.5 -> 21.0.
//
//  NOTHING HERE IS DIMMED.  The lowest-contrast text on this theme is the
//  keyword purple at 7.6:1, well clear of the 4.5:1 body-text threshold.  A
//  theme reads as dark because the ground is dark, not because the text on it
//  has been faded.
//
//  THE BARS ARE THE DARK THEME'S, EXACTLY.  Editor token colours are chosen to
//  sit in a page of text without shouting, which is the opposite of what a
//  300 px block of fill has to do - the function-name yellow (#DCDCAA) and the
//  chart green (#89D185) came out looking chalky next to the plain dark theme's
//  pure ones.  So green, red, orange and the bar fill are lifted verbatim from
//  DARK above.  They are signal colours, not syntax colours: what they mean is
//  "power", "alarm", "warning", and none of those has an equivalent in a
//  stylesheet for source code.
//*********************************************************************************
static const Palette DARK2026 = {
  //  Black ground, pure white ink.  VS Code's own foreground is #CCCCCC, but the
  //  headline number is white here on purpose - see `reading` below.
  /* black  */ 0x000000, /* white  */ 0xFFFFFF,

  //  Editor foreground for secondary text (13.1:1) and descriptionForeground
  //  for hints and button borders (7.7:1).  Neither is a "dim" grey; the second
  //  tier is a step, not a fade.
  /* grey   */ 0xCCCCCC, /* dkGrey */ 0x9D9D9D,

  //  DARK's green and red verbatim: the PEP tail on the power bar and the two
  //  lower bands of the SWR bar are drawn from these, and the bars have to match
  //  the dark theme exactly.
  /* green  */ 0x00FF00, /* red    */ 0xFF0000,

  //  The control-keyword purple, carrying every caption and screen title: it is
  //  the colour an editor uses for the word that NAMES what follows, which is
  //  exactly what "Fwd:" and "Pk:" are.
  /* yellow */ 0xC586C0,

  //  DARK's amber: it is the SWR bar's middle band before it is anything else.
  /* orange */ 0xFFB600,

  //  Variable light-blue and regex red.
  /* cyan   */ 0x9CDCFE, /* pink   */ 0xD16969,

  //  The declaration-keyword blue for the touch-calibration crosshair.
  /* ltBlue */ 0x569CD6,

  //  input/dropdown background for the raised face, and VS Code's own focus blue
  //  for the press - the same blue its buttons use, so the flash is familiar.
  /* btnBg  */ 0x313131, /* btnAct */ 0x0078D4,

  //  The focus blue again for the selected row (4.6:1 against black), with white
  //  on it (4.5:1).  VS Code's list.activeSelectionBackground (#04395E) is only
  //  1.7:1 here - the third time a themed selection band has come out too faint
  //  to find, so it is measured rather than copied.
  /* menuHi */ 0x0078D4, /* menuHiText */ 0xFFFFFF,

  //  charts.orange: the "simulated" marker has to read as a state, not decor.
  /* sim    */ 0xD18616,

  //  DARK's pure yellow on DARK's black trough: the power bar is pixel-identical
  //  between the two themes.
  /* barLive*/ 0xFFFF00, /* barTrough */ 0x000000,

  //  White for the number and the type teal for SWR.  The caption already
  //  carries the purple, so the value itself stays uncoloured - in an editor the
  //  keyword is highlighted and the literal is not, and a reading you check at a
  //  glance is easiest to find when it is the plainest thing on the screen.
  /* reading*/ 0xFFFFFF, /* swrTxt    */ 0x4EC9B0,
};

static void loadPalette(const Palette& p)
{
  cBlack  = lv_color_hex(p.black);
  cWhite  = lv_color_hex(p.white);
  cGrey   = lv_color_hex(p.grey);
  cDkGrey = lv_color_hex(p.dkGrey);
  cGreen  = lv_color_hex(p.green);
  cRed    = lv_color_hex(p.red);
  cYellow = lv_color_hex(p.yellow);
  cOrange = lv_color_hex(p.orange);
  cCyan   = lv_color_hex(p.cyan);
  cPink   = lv_color_hex(p.pink);
  cLtBlue = lv_color_hex(p.ltBlue);
  cBtnBg  = lv_color_hex(p.btnBg);
  cBtnAct = lv_color_hex(p.btnAct);
  cMenuHi     = lv_color_hex(p.menuHi);
  cMenuHiText = lv_color_hex(p.menuHiText);
  cSim        = lv_color_hex(p.sim);
  cBarLive    = lv_color_hex(p.barLive);
  cBarTrough  = lv_color_hex(p.barTrough);
  cReading    = lv_color_hex(p.reading);
  cSwrTxt     = lv_color_hex(p.swrTxt);
}

void begin(ThemeMode theme)
{
  //  lv_style_init() ONCE per style, here.  Calling it again on a style that is
  //  already attached to objects would drop its property array on the floor,
  //  which is why applyTheme() only ever re-runs the setters.
  lv_style_init(&styleBtn);
  lv_style_init(&styleBtnPressed);
  lv_style_init(&styleBtnLocked);
  lv_style_init(&stylePanel);
  lv_style_init(&styleScreenBg);
  lv_style_init(&styleTitle);
  lv_style_init(&styleFrame);
  lv_style_init(&styleRow);
  lv_style_init(&styleRowSelected);

  applyTheme(theme);
}

void applyTheme(ThemeMode theme)
{
  switch (theme) {
    case ThemeMode::Light:    loadPalette(LIGHT);    break;
    case ThemeMode::Dark2026: loadPalette(DARK2026); break;
    default:                  loadPalette(DARK);     break;
  }
  refreshStyles();

  //  Tell LVGL that styles already in use have changed.  NULL means "every
  //  style", which is right here: the alternative is naming all nine, and
  //  forgetting one would leave a stripe of the old theme behind.
  lv_obj_report_style_change(nullptr);
}

static void refreshStyles()
{
  lv_style_set_radius(&styleBtn, 6);
  lv_style_set_bg_color(&styleBtn, cBtnBg);
  lv_style_set_bg_opa(&styleBtn, LV_OPA_COVER);
  lv_style_set_border_color(&styleBtn, cDkGrey);
  lv_style_set_border_width(&styleBtn, 1);
  lv_style_set_text_color(&styleBtn, cWhite);
  lv_style_set_text_font(&styleBtn, UI_FONT_MED);
  //  No shadow, anywhere.  It is the most expensive thing LVGL can draw and
  //  this SPI budget will not carry it.
  lv_style_set_shadow_width(&styleBtn, 0);

  lv_style_set_bg_color(&styleBtnPressed, cBtnAct);

  //  Added on top of styleBtn, so it only has to override what differs.
  lv_style_set_bg_color(&styleBtnLocked, cBlack);
  lv_style_set_text_color(&styleBtnLocked, cDkGrey);

  lv_style_set_radius(&stylePanel, 0);
  lv_style_set_bg_color(&stylePanel, cBlack);
  lv_style_set_bg_opa(&stylePanel, LV_OPA_COVER);
  lv_style_set_border_width(&stylePanel, 0);
  lv_style_set_pad_all(&stylePanel, 0);
  lv_style_set_shadow_width(&stylePanel, 0);

  lv_style_set_bg_color(&styleScreenBg, cBlack);
  lv_style_set_bg_opa(&styleScreenBg, LV_OPA_COVER);
  lv_style_set_pad_all(&styleScreenBg, 0);

  lv_style_set_text_font(&styleTitle, UI_FONT_LARGE);
  lv_style_set_text_color(&styleTitle, cYellow);

  lv_style_set_radius(&styleFrame, 0);
  lv_style_set_bg_color(&styleFrame, cBlack);
  lv_style_set_bg_opa(&styleFrame, LV_OPA_COVER);
  lv_style_set_border_color(&styleFrame, cWhite);
  lv_style_set_border_width(&styleFrame, 1);
  lv_style_set_pad_all(&styleFrame, 0);
  lv_style_set_shadow_width(&styleFrame, 0);

  lv_style_set_radius(&styleRow, 0);
  lv_style_set_bg_color(&styleRow, cBlack);
  lv_style_set_bg_opa(&styleRow, LV_OPA_COVER);
  lv_style_set_border_width(&styleRow, 0);
  lv_style_set_pad_all(&styleRow, 0);
  lv_style_set_pad_left(&styleRow, 4);
  lv_style_set_text_color(&styleRow, cGrey);
  lv_style_set_text_font(&styleRow, UI_FONT_MED);
  lv_style_set_shadow_width(&styleRow, 0);

  //  Its own palette entry rather than reusing cBlack, which is what this used
  //  to do.  cBlack means "the canvas", and on the light theme that made the
  //  selected row near-white text on a solid block - the one row you are
  //  pointing at became the one row that did not match the others.  Now each
  //  theme states it: inverted on dark, unchanged on light.
  lv_style_set_bg_color(&styleRowSelected, cMenuHi);
  lv_style_set_text_color(&styleRowSelected, cMenuHiText);
}

void styleScreen(lv_obj_t* scr)
{
  //  remove-then-add: this is called twice for every pushed screen (once by the
  //  screen's own build(), once by UiApp::doPush), and lv_obj_add_style has no
  //  duplicate check of its own.
  lv_obj_remove_style(scr, &styleScreenBg, 0);
  lv_obj_add_style(scr, &styleScreenBg, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* makePanel(lv_obj_t* parent)
{
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_add_style(o, &stylePanel, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

}  // namespace UiTheme

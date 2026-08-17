//*********************************************************************************
//  AboutScreen.h  -  Who wrote it, under what licence, and what it is calibrated
//                    to right now.
//
//  The second half is the useful half.  A power meter's readings are only
//  meaningful together with the calibration behind them, and those numbers
//  otherwise live in NVS where nobody can see them: this is the screen you read
//  out loud when someone asks why two meters disagree, and the one to photograph
//  before touching the calibration procedure.
//
//  Content is built once and never updated - everything here changes only via
//  the calibration screens, which are pushed on top of the menu, so returning
//  here rebuilds it anyway.
//*********************************************************************************
#pragma once

#include "Screen.h"

class AboutScreen : public Screen {
public:
  void build() override;

private:
  void onTap(lv_event_t* e);

  //  Adds one line to the scrolling body and returns it, so callers can restyle.
  lv_obj_t* addLine(lv_obj_t* parent, const char* txt,
                    const lv_font_t* font, lv_color_t colour);

  bool going_ = false;
};

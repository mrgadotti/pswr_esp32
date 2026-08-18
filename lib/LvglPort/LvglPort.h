//*********************************************************************************
//  LvglPort.h  -  Binds LVGL to the panel hardware.
//
//  The ONLY translation unit in the project that includes both lvgl.h and
//  TFT_eSPI.h.  Ili9341Driver stays a pure hardware abstraction; everything
//  LVGL-specific about the panel and the touch controller lives here:
//    - the display driver (draw buffers + flush_cb over DMA),
//    - the input driver (read_cb wrapping Ili9341Driver::readTouch),
//    - jitter filtering and the coordinate latch LVGL needs on release.
//
//  THREADING.  LVGL 8 is not thread-safe.  Every lv_* call in this firmware -
//  including begin() and the lv_timer_handler() pump - must happen in the core-1
//  UI task and nowhere else.  Core 0 never touches LVGL.
//*********************************************************************************
#pragma once

#include <lvgl.h>

#include "Ili9341Driver.h"

class LvglPort {
public:
  //  Draw buffer geometry.  320 x 24 is 1/10 of the panel - LVGL's documented
  //  sweet spot, and a whole number of full rows so every flush maps to one
  //  contiguous ILI9341 address window.  Two buffers so rendering can overlap
  //  the DMA push.  See LvglPort.cpp for why not bigger.
  static constexpr uint32_t BUF_ROWS = 24;
  static constexpr uint32_t BUF_PX   = (uint32_t)SCREEN_W * BUF_ROWS;

  //  lv_init() + register the display and input drivers.  Call from the UI task
  //  itself (not from setup()) so there is never a window in which two tasks
  //  could both be "the LVGL task".
  bool begin(Ili9341Driver& hw);

  //  Wait out the last DMA push of a refresh and release the SPI bus.  Call once
  //  per UI iteration, immediately after lv_timer_handler().
  //
  //  MANDATORY, not optional.  flush() deliberately leaves its transfer running
  //  so LVGL can render the next area against it; the final transfer of a
  //  refresh has no next flush to close it, and would otherwise sit there with
  //  the bus locked until whenever the screen next changes.  Costs nothing when
  //  no push is outstanding.
  void endFrame();

  //  Bring-up telemetry: how much the panel is actually being asked to redraw.
  //  Expected pixels/s is the only way to tell "my widget is slow" apart from
  //  "something is invalidating far more area than I think".
  struct FlushStats { uint32_t calls; uint32_t pixels; uint32_t maxPixels; };
  static FlushStats takeFlushStats();   // reads and resets

private:
  //  LVGL C callbacks -> instance, via drv->user_data.
  static void flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px);
  static void touchCb(lv_indev_drv_t* drv, lv_indev_data_t* data);

  void flush(const lv_area_t* area, lv_color_t* px);
  void readTouch(lv_indev_data_t* data);

  Ili9341Driver* hw_ = nullptr;

  lv_disp_draw_buf_t drawBuf_{};
  lv_disp_drv_t      dispDrv_{};
  lv_indev_drv_t     indevDrv_{};

  //  Last touch position, carried forward so the RELEASED report still has a
  //  valid point - that is what LVGL uses to decide whether to emit CLICKED.
  lv_coord_t lastX_ = 0;
  lv_coord_t lastY_ = 0;

  //  Distinguishes "a new press starts here" from "this press continues", so
  //  the smoothing filter is reset per touch instead of blending across touches.
  bool wasPressed_ = false;

  //  A DMA push was started and its endWrite() has been deferred.  At most one
  //  can ever be outstanding: every flush closes the previous one before it
  //  opens its own.
  bool pushPending_ = false;
};

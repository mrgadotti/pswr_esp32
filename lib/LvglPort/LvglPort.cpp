//*********************************************************************************
//  LvglPort.cpp
//*********************************************************************************
#include "LvglPort.h"

//*********************************************************************************
//  DRAW BUFFERS
//
//  In .bss (DRAM_ATTR, 4-byte aligned) rather than the heap: internal DRAM is
//  DMA-capable and, crucially, static allocation is NOT subject to the largest
//  contiguous free-block ceiling - measured at 110,580 B on this board, which is
//  why a full 320x240x16bpp framebuffer (153,600 B) is off the table entirely.
//
//  Why 24 rows and not more: LVGL renders a whole buffer before flushing it, so
//  a taller buffer leaves the panel idle during the render and then bursts.
//  Smaller buffers pipeline better against DMA.  24 rows already covers the
//  tallest frequently-redrawn widget (the 28 px power bar).
//
//  Cost: 2 x 320 x 24 x 2 B = 30,720 B.
//*********************************************************************************
static DRAM_ATTR __attribute__((aligned(4)))
lv_color_t lvBuf1[LvglPort::BUF_PX];

static DRAM_ATTR __attribute__((aligned(4)))
lv_color_t lvBuf2[LvglPort::BUF_PX];

//*********************************************************************************
//  DISPLAY FLUSH  -  overlapped.
//
//  The push is left IN FLIGHT and lv_disp_flush_ready() is called immediately,
//  so LVGL renders the next area into the other draw buffer while the panel is
//  still draining this one.  Without that the two buffers cost 30 KB and buy
//  nothing: endWrite() carries TFT_eSPI's DMA_BUSY_CHECK, so the blocking form
//  parked the CPU for the whole transfer - ~22 ms of SPI on a full-screen
//  repaint at 55 MHz, which is what a screen change costs.
//
//  THE TRAP, and how this avoids it.  pushPixelsDMA() self-synchronises (it
//  calls dmaWait() first), but setAddrWindow() does NOT - it pokes
//  SPI_CMD_REG/SPI_W0_REG directly while the IDF spi_master driver may still
//  have a transaction in flight, and the result is intermittent corruption
//  under load.  So the previous push is closed at the TOP of the next flush,
//  before any register is touched: endWrite() is exactly dmaWait() plus the bus
//  release, which is both halves of what is needed.
//
//  Correctness rests on one invariant: at most one transfer is ever in flight,
//  and it is always drained before LVGL could re-render the buffer feeding it.
//  With two buffers LVGL alternates, so a buffer is re-rendered no earlier than
//  two flushes after its own - and every flush drains the one before it.
//
//  endFrame() closes the last push of a refresh.  It is not an optimisation but
//  a requirement: without it the final transfer stays in flight with the bus
//  locked until the next flush, which may be a whole idle second away.
//
//  No byte swapping happens here: LV_COLOR_16_SWAP is 1, so LVGL already emits
//  big-endian RGB565 as part of the blend, and TFT_eSPI's _swapBytes stays
//  false - making this a genuinely zero-copy path.
//*********************************************************************************
static LvglPort::FlushStats gFlush{};

LvglPort::FlushStats LvglPort::takeFlushStats()
{
  FlushStats out = gFlush;
  gFlush = FlushStats{};
  return out;
}

void LvglPort::flush(const lv_area_t* area, lv_color_t* px)
{
  TFT_eSPI& tft = hw_->tft();
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;

  gFlush.calls++;
  gFlush.pixels += w * h;
  if (w * h > gFlush.maxPixels) gFlush.maxPixels = w * h;

  //  Close the PREVIOUS push first: endWrite() is DMA_BUSY_CHECK plus the bus
  //  release, so this is the dmaWait() that setAddrWindow() below needs and
  //  cannot do for itself.
  if (pushPending_) { tft.endWrite(); pushPending_ = false; }

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushPixelsDMA(reinterpret_cast<uint16_t*>(px), w * h);
  pushPending_ = true;

  //  Ready NOW, with the transfer still running.  Safe only because there are
  //  two draw buffers - LVGL will render into the other one.
  lv_disp_flush_ready(&dispDrv_);
}

void LvglPort::endFrame()
{
  if (!pushPending_) return;
  hw_->tft().endWrite();
  pushPending_ = false;
}

void LvglPort::flushCb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px)
{
  static_cast<LvglPort*>(drv->user_data)->flush(area, px);
}

//*********************************************************************************
//  TOUCH
//
//  Ili9341Driver::readTouch() already does the raw->screen mapping and, on the
//  two XPT2046 boards, the pressure gate and the tirqTouched() short-circuit
//  (which keeps an untouched poll to a single GPIO read instead of an SPI
//  transaction - the FT6336 board has no IRQ short-circuit of its own, see
//  Ili9341Driver.cpp, so an idle poll there costs one small I2C transaction
//  instead).  What LVGL adds on top, the same on every board:
//
//  1. The coordinate latch.  readTouch() returns false without writing x/y, and
//     LVGL needs a valid point on the RELEASED report too - it compares it
//     against the pressed object to decide whether this was a click.  Reporting
//     (0,0) on release silently swallows every click.
//  2. Jitter filtering.  A resistive panel wanders a few pixels; 76 px buttons
//     hide it today, but it shows up as drift once lists scroll kinetically.
//     Harmless on the capacitive FT6336 board, which does not need it.
//*********************************************************************************
void LvglPort::readTouch(lv_indev_data_t* data)
{
  int x, y;
  if (hw_->readTouch(x, y))
  {
    if (!wasPressed_) {
      //  FIRST report of a new press: snap, never blend.  LVGL decides which
      //  object was hit from this very sample, so carrying the previous touch's
      //  position into it makes a tap land halfway to wherever the user last
      //  touched - which reads as the UI acting on a stale, lagging position.
      lastX_ = (lv_coord_t)x;
      lastY_ = (lv_coord_t)y;
      wasPressed_ = true;
    } else {
      //  Within one continuous press, a one-pole IIR (alpha = 1/2) smooths the
      //  wander of the resistive layer without adding cross-touch memory.
      lastX_ = (lv_coord_t)((lastX_ + x) / 2);
      lastY_ = (lv_coord_t)((lastY_ + y) / 2);
    }
    data->state = LV_INDEV_STATE_PRESSED;
  }
  else
  {
    wasPressed_ = false;
    data->state = LV_INDEV_STATE_RELEASED;
  }

  data->point.x = lastX_;
  data->point.y = lastY_;
}

void LvglPort::touchCb(lv_indev_drv_t* drv, lv_indev_data_t* data)
{
  static_cast<LvglPort*>(drv->user_data)->readTouch(data);
}

//*********************************************************************************
//  LIFECYCLE
//*********************************************************************************
bool LvglPort::begin(Ili9341Driver& hw)
{
  hw_ = &hw;

  //  Rotation and inversion stay in Ili9341Driver::begin(); LVGL is told the
  //  post-rotation geometry and never touches sw_rotate.
  if (!hw_->beginDma()) return false;

  lv_init();

  lv_disp_draw_buf_init(&drawBuf_, lvBuf1, lvBuf2, BUF_PX);

  lv_disp_drv_init(&dispDrv_);
  dispDrv_.hor_res   = SCREEN_W;
  dispDrv_.ver_res   = SCREEN_H;
  dispDrv_.draw_buf  = &drawBuf_;
  dispDrv_.flush_cb  = flushCb;
  dispDrv_.user_data = this;
  if (!lv_disp_drv_register(&dispDrv_)) return false;

  lv_indev_drv_init(&indevDrv_);
  indevDrv_.type      = LV_INDEV_TYPE_POINTER;
  indevDrv_.read_cb   = touchCb;
  indevDrv_.user_data = this;
  if (!lv_indev_drv_register(&indevDrv_)) return false;

  return true;
}

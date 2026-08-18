//*********************************************************************************
//  AppConfig.h  -  Meter configuration constants.
//
//  The measurement constants are kept BYTE-FOR-BYTE identical to the reference
//  firmware, because the calibration stored in a real meter's NVS depends on
//  them: changing a value here changes what the instrument reads.
//*********************************************************************************
#pragma once

#include <stdint.h>

//  Firmware version.  A macro, not a constexpr, because the boot screen builds
//  its string by literal concatenation:  "v" VERSION.
#define VERSION   "1.00"

//*********************************************************************************
//  AD8307 two-point calibration defaults (30:1 Tandem Match)
//*********************************************************************************
constexpr int16_t CAL1_NOR_VALUE  = 400;      // 40.0 dBm
constexpr int16_t CAL2_NOR_VALUE  = 100;      // 10.0 dBm
constexpr float   CALFWD1_DEFAULT = 2.233f;
constexpr float   CALREV1_DEFAULT = 2.233f;
constexpr float   CALFWD2_DEFAULT = 1.528f;
constexpr float   CALREV2_DEFAULT = 1.528f;
constexpr double  LOGAMP_SLOPE    = 23.5;     // mV/dB nominal (One-Level cal)
constexpr double  CAL_INP_QUALITY = 12.0;     // dB min Fwd/Rev separation to accept cal
constexpr int16_t CAL_DBM_MAX     = 530;
constexpr int16_t CAL_DBM_MIN     = -100;

//  Smallest usable spread between the two calibration points, in detector volts.
//  Below this the two-point fit degenerates and its slope is meaningless - the
//  default pair spans 0.705 V, so 0.05 is already 14x too flat.
//
//  Used in BOTH places that care, deliberately: the Calibrate screen warns when
//  a stored pair falls under it, and PowerMath::determineDbm() refuses to divide
//  by it.  One number, because a warning the math does not honour is decoration.
constexpr double  CAL_MIN_SPAN_V  = 0.05;

//*********************************************************************************
//  Diode / Bruene bridge defaults
//*********************************************************************************
constexpr double BRIDGE_COUPLING = 24.0;      // transformer turns ratio (24:1)
constexpr float  METER_CAL       = 1.00f;
constexpr double D_VDROP         = 0.25;

//*********************************************************************************
//  Power / SWR thresholds (mW)
//*********************************************************************************
constexpr double   MIN_PWR_FOR_SWR_CALC_AD8307 = 20.0;
constexpr double   MIN_PWR_FOR_SWR_CALC_DIODE  = 30.0;
constexpr uint8_t  SWR_ALARM_DEFAULT     = 30;   // 3.0:1 (40 disables)
constexpr uint16_t SWR_THRESHOLD_DEFAULT = 10;   // mW

//*********************************************************************************
//  Backlight
//
//  The floor is not cosmetic.  A backlight that can be taken to zero from a menu
//  is a backlight that can leave the operator holding a black panel with no way
//  to read the control that would bring it back - and on a resistive panel there
//  is no muscle memory to fall back on.  10% is still clearly legible indoors.
//
//  PWM: LEDC channel 0 at 5 kHz / 8 bits.  Above the audible range (both boards
//  drive the LED string through a small transistor that will sing at 1 kHz) and
//  far below the point where the ESP32's 80 MHz LEDC clock loses resolution.
//  A board whose backlight pin is not a GPIO at all sets BOARD_HAS_BACKLIGHT_PWM
//  to 0 and simply stores the setting - see Ili9341Driver::setBrightness().
//*********************************************************************************
constexpr uint8_t  BRIGHTNESS_MIN     = 10;    // percent
constexpr uint8_t  BRIGHTNESS_MAX     = 100;
constexpr uint8_t  BRIGHTNESS_DEFAULT = 100;
constexpr uint8_t  BRIGHTNESS_STEP    = 5;

constexpr uint8_t  BACKLIGHT_PWM_CH   = 0;
constexpr uint32_t BACKLIGHT_PWM_HZ   = 5000;
constexpr uint8_t  BACKLIGHT_PWM_BITS = 8;

//*********************************************************************************
//  Autoscale ranges, up to 3 per decade (... 11 22 55 110 220 550 ...)
//*********************************************************************************
constexpr uint8_t SCALE_RANGE1 = 11;
constexpr uint8_t SCALE_RANGE2 = 22;
constexpr uint8_t SCALE_RANGE3 = 55;

//*********************************************************************************
//  Sampling / buffers (ADS1015 over I2C ~ 500 pairs/s)
//*********************************************************************************
constexpr uint32_t SAMPLE_MS    = 2;
constexpr uint16_t BUF_SHORT    = 50;          // 100 ms
constexpr uint16_t AVG_BUF1S    = 500;
constexpr uint16_t AVG_BUFSHORT = 50;
constexpr uint16_t AVG_BUFSWR   = 10;
constexpr uint32_t POLL_MS      = 10;

//  PEP envelope options selectable from the front panel / menu, in BUF_SHORT
//  units: 100 ms, 1 s, 2.5 s windows.  (Labels: PEP_LABELS in Ui/MenuScreen.cpp.)
constexpr uint16_t PEP_OPTIONS[3] = { 1, 10, 25 };

//  The PEP buffer is sized for the largest selectable window, not the active
//  one, so switching the window at runtime never reallocates or overruns.
constexpr uint16_t PEP_MAX_WINDOW = 25;

//  Accumulator type for the two sliding-sum power averages.
//
//  MUST STAY `double`.  This used to read "switch to float to halve the largest
//  RAM cost, measure before switching", which invites the wrong move: the issue
//  is not precision per reading, it is that these accumulators are NEVER RESET.
//  PowerMath keeps them as `accum += newest; accum -= oldest` for the whole
//  session (only reset() clears them, i.e. a coupler change), so every rounding
//  error is retained and they random-walk with the sample count.
//
//  The failure that matters is not the slow drift, it is the residue.  Run a
//  1 kW carrier and the 1 s sum reaches ~5e8 mW, where a float ULP is ~32; stop
//  transmitting and the 500 subtractions that should empty the window instead
//  leave whatever did not cancel.  On an idle meter that is a phantom average
//  power that never decays.  In double the same residue is ~1e-6 mW.
//
//  If these 4.4 KB are ever genuinely needed, shorten the WINDOWS (AVG_BUF1S) or
//  drop the averages outright - do not narrow the accumulator.
using AvgSample = double;

//*********************************************************************************
//  MEMORY BUDGET  -  every knob worth turning, with what it actually costs.
//
//  Measured at 85,708 B static RAM (26.2% of 320 KB) with 270 KB of heap free,
//  so nothing here is urgent - this is a map for the day something bigger has to
//  fit, not a list of problems.  Ordered by size.
//
//  | Knob                        | Where            | Cost now | Notes
//  |-----------------------------|------------------|----------|--------------
//  | LvglPort::BUF_ROWS = 24     | LvglPort.h       | 30,720 B | 2 buffers of
//  |                             |                  |          | 320 x rows x 2 B.
//  |                             |                  |          | Halving to 12
//  |                             |                  |          | frees 15 KB and
//  |                             |                  |          | costs more
//  |                             |                  |          | flushes per
//  |                             |                  |          | frame; going to
//  |                             |                  |          | one buffer frees
//  |                             |                  |          | 15 KB more but
//  |                             |                  |          | rules out ever
//  |                             |                  |          | overlapping the
//  |                             |                  |          | DMA push.
//  | LV_MEM_SIZE = 24 KB         | include/lv_conf.h| 24,576 B | Measured 32%
//  |                             |                  |          | used, 0% frag.
//  |                             |                  |          | 16 KB would fit
//  |                             |                  |          | today; leave
//  |                             |                  |          | room for new
//  |                             |                  |          | screens.
//  | UI_TASK_STACK = 8 KB        | below            |  8,192 B | Heap, not .bss.
//  |                             |                  |          | Measured high
//  |                             |                  |          | water ~2,964 B.
//  | METER_TASK_STACK = 6 KB     | below            |  6,144 B | Heap.  double
//  |                             |                  |          | pow()/log10().
//  | AVG_BUF1S = 500             | below            |  4,000 B | AvgSample is
//  |                             |                  |          | double; switch
//  |                             |                  |          | it to float to
//  |                             |                  |          | halve this and
//  |                             |                  |          | AVG_BUFSHORT.
//  | Montserrat 48 px            | include/lv_conf.h| ~55 KB   | FLASH, not RAM.
//  |                             |                  |          | A digits-only
//  |                             |                  |          | subset is ~16 KB.
//  | ScopeWidget::SCOPE_BUF      | ScopeWidget.h    |    608 B | One int16 per
//  |                             |                  |          | trace column.
//  | ListScreen MAX_ITEMS x LEN  | ListScreen.h     |    320 B | Per open list
//  |                             |                  |          | screen, on the
//  |                             |                  |          | C++ heap (the
//  |                             |                  |          | screen object is
//  |                             |                  |          | new'd; only its
//  |                             |                  |          | LVGL objects come
//  |                             |                  |          | from the pool).
//
//  The number that actually constrains the draw buffer is NOT total free heap
//  but the largest contiguous DMA-capable block - measured at 110,580 B here,
//  which is why the buffers are static (.bss) instead of malloc'd.
//*********************************************************************************

//*********************************************************************************
//  FreeRTOS task layout
//
//  Core 0  "meter"  - ADS1015 sampling + power/PEP/SWR math, every SAMPLE_MS.
//  Core 1  "ui"     - panel, touch and LEDs, every POLL_MS.
//
//  Both run above the Arduino loop task priority (1), which setup() deletes once
//  the two tasks are up.
//*********************************************************************************
constexpr uint8_t  METER_TASK_CORE  = 0;
constexpr uint32_t METER_TASK_STACK = 6144;   // math uses double pow()/log10()
constexpr uint8_t  METER_TASK_PRIO  = 3;      // must not miss its 2 ms slot

constexpr uint8_t  UI_TASK_CORE     = 1;
constexpr uint8_t  UI_TASK_PRIO     = 2;

//  8 KB.  LVGL's software renderer recurses lv_refr_area -> lv_obj_redraw ->
//  lv_draw_sw_letter and puts a glyph bitmap plus radius-mask temporaries on the
//  stack, with the 48 px anti-aliased font as the worst case - so this started
//  at 16 KB out of caution.  Measured on hardware across every screen, the high
//  water mark was ~2,960 B, so 8 KB keeps a 2.7x margin at half the cost.
//  (uxTaskGetStackHighWaterMark returns BYTES on ESP-IDF, not words.)
constexpr uint32_t UI_TASK_STACK    = 8192;

//*********************************************************************************
//  UI cadence
//
//  UI_TICK_MS drives lv_timer_handler().  It must be SHORTER than LVGL's own
//  LV_DISP_DEF_REFR_PERIOD (10 ms in lv_conf.h) - calling at exactly the refresh
//  period produces beat-frequency stutter where some invocations do two periods'
//  work and some do none.
//
//  METER_TICK_MS is a separate, drift-free accumulator, because two consumers
//  are clocked in TICKS rather than milliseconds and neither looks like a timer:
//    - ModulationScope::add() is one column per tick; 300 columns = one sweep.
//    - scaleRanges() keeps a 30-sample sliding maximum; 30 ticks = the autoscale
//      hold time.
//  Measured on hardware, the old TFT_eSPI ui task actually ran at ~19.7 ms (it
//  slept POLL_MS *after* a frame costing up to 12.2 ms), making the sweep ~5.9 s
//  and the hold ~590 ms.  Targeting the intended 10 ms here is a deliberate
//  choice: the sweep becomes 3.0 s and the hold 300 ms, i.e. visibly faster than
//  the old firmware.
//*********************************************************************************
constexpr uint32_t UI_TICK_MS    = 5;
constexpr uint32_t METER_TICK_MS = 10;

//*********************************************************************************
//  MOCK  -  no ADS1015: synthesise detector voltages from the current
//  calibration, so the whole interface can be exercised without RF hardware.
//
//  This is a RANDOM WALK, not the original's fixed 129.5 W.  A frozen reading
//  makes every widget hold still, which hides real cost: measured on hardware,
//  a still UI costs ~285 us per frame and a moving one ~4,400 us.  A meter that
//  looks free on the bench and then struggles on real RF is the worst outcome,
//  so the mock moves.
//
//  TWO simulated couplers, not one.  The coupler selector, the per-coupler
//  calibration and the meter task's mid-run detector swap are otherwise
//  unreachable without two converters strapped to different addresses - and
//  being unreachable is exactly why they are worth simulating.  They are
//  deliberately different instruments: a barefoot rig on CPL1, an amplifier on
//  CPL2, so switching between them changes the decade, the autoscale range and
//  the SWR band all at once.
//*********************************************************************************
struct MockProfile {
  double centreW;          // power the walk reverts to
  double minW, maxW;       // hard limits
  double minSwr, maxSwr;   // SWR band; its centre and step size are derived
};

constexpr uint8_t MOCK_COUPLERS = 2;

//  centreW is NOT the middle of [minW, maxW]: it is set so that the walk's
//  typical excursion (see MOCK_STEP_DB below, ~1.6 dB at 2 sigma = x1.46) just
//  reaches maxW.  Picking the geometric middle instead would leave the ceiling
//  decorative - the walk would never get near it.
constexpr MockProfile MOCK_PROFILES[MOCK_COUPLERS] = {
  //  CPL1: 100 W class transceiver into a mediocre load.
  {   75.0, 0.05,  110.0,   1.1, 2.5 },
  //  CPL2: amplifier into a good one.
  { 1100.0, 0.50, 1600.0,   1.1, 1.5 },
};

//  The walk is multiplicative (a step in dB, not in watts) with mean reversion
//  toward centreW.  Step and reversion are tuned together: with uniform steps in
//  +/-s the stationary spread is (s/sqrt(3)) / sqrt(2 * revert), i.e. ~0.82 dB
//  here, so the reading wanders roughly +/-1.6 dB and crosses one or two
//  autoscale range boundaries.  Both must shrink together to slow the walk down
//  without also narrowing it.  That matters twice over: a transmitter does not
//  jitter tens of dB per second, and a mock that moves on every 2 ms sample
//  makes every label redraw on every UI update.
constexpr double MOCK_STEP_DB    = 0.02;   // max power step per 2 ms sample
constexpr double MOCK_REVERT     = 0.00010;

//  Only the reversion rate is fixed for SWR; the step is derived per profile
//  from the same relation, so that each coupler's walk actually covers the band
//  it advertises instead of sitting in the middle of it.  1/MOCK_SWR_REVERT
//  samples is the time constant: ~17 s, so it drifts rather than flickers.
constexpr double MOCK_SWR_REVERT = 0.00012;

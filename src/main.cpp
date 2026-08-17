//*********************************************************************************
//**
//** PSWR_CYD  -  RF Power & SWR Meter for the ESP32 Cheap Yellow Display
//**
//** Author..: Marcelo R. Gadotti (PP5MGT)
//** Version.: 1.00
//**
//** A ground-up implementation: dual-core measurement engine, a detector
//** abstraction, and an LVGL user interface, none of which exist in any earlier
//** work.  The Multi Display RF Power and SWR Meter by Loftur E. Jonasson
//** (TF3LJ/VE2AO), with later additions by J.G. Holstein, was used as the
//** REFERENCE for the instrument's behaviour - and its measurement algorithms
//** (the AD8307 two-point dBm conversion, the peak/PEP/averaging windows and the
//** calibration procedure, all now in PowerMath) were carried over faithfully on
//** purpose: a meter that reads differently from the one its users calibrated
//** against would be a worse instrument, not a better one.
//**
//** Target..: ESP32-2432S028R "Cheap Yellow Display" (CYD)
//**           - ILI9341 320x240 via TFT_eSPI (pins in platformio.ini build flags)
//**           - XPT2046 resistive touch on its own VSPI bus
//**           - ADS1015 (I2C) replaces the AD7991; AIN0=Fwd, AIN1=Rev
//**           - dual core: measurement on core 0, user interface on core 1
//**           - user interface built on LVGL 8.4
//**
//** If no ADS1015 is detected at boot, the meter falls back to a simulated
//** detector so the whole interface stays exercisable without RF hardware.
//**
//** This file holds ONLY the application orchestration.  Responsibilities live
//** in dedicated modules:
//**   ConfigManager  - settings + NVS persistence
//**   RfDetectors    - ADS1015 front-end and the simulator (RfDetector impls)
//**   PowerMath      - detector volts -> power/PEP/SWR.  No HW, no RTOS.
//**   MeterEngine    - core-0 task wiring a detector to the math
//**   Ili9341Driver  - TFT_eSPI panel and XPT2046 touch (hardware only)
//**   LvglPort       - LVGL display + input drivers, draw buffers, DMA flush
//**   Ui/            - screens, widgets, theme, formatting, screen stack
//**
//** The two cores are split strictly by concern.  The meter task (core 0, owned
//** by MeterEngine) samples and computes; the UI task below (core 1) draws and
//** reads the touch panel.  They meet at exactly one point: the MeterReadings
//** snapshot, pulled by meter.syncReadings() once per UI frame.  A slow redraw
//** or an open menu therefore cannot disturb the measurement cadence.
//**
//** LVGL 8 is NOT thread-safe.  Every lv_* call in this firmware happens inside
//** uiTask, and there is no compiler enforcement of that - a stray lv_* from
//** core 0 in some future refactor would be a heisenbug worth days.
//**
//** Licensed under the GNU General Public License v3 or later, as the reference
//** work is.  This program comes with ABSOLUTELY NO WARRANTY.  See the full
//** licence text at <http://www.gnu.org/licenses/>.
//*********************************************************************************
#include <Arduino.h>

#include "AppConfig.h"
#include "Pins.h"

#include "BootScreen.h"
#include "ConfigManager.h"
#include "MeterEngine.h"
#include "Ili9341Driver.h"
#include "LvglPort.h"
#include "UiApp.h"
#include "UiTheme.h"

#if BRINGUP_STATS
  #include <esp_heap_caps.h>
#endif

ConfigManager config;
MeterEngine   meter;
Ili9341Driver display;
LvglPort      lvgl;
UiApp         ui;

static TaskHandle_t uiTaskHandle = nullptr;

#if BRINGUP_STATS
//*********************************************************************************
//  BRING-UP INSTRUMENTATION  -  compiled out entirely without -D BRINGUP_STATS=1.
//
//  The last bench-only flag; drop it for a release build.  Reported once a second:
//    heap    free / minimum-ever free / largest contiguous DMA-capable block
//              (that last one is what caps an LVGL draw buffer, NOT the total)
//    ui      worst and mean time of one lv_timer_handler() pass
//    stack   ui task headroom in BYTES (ESP-IDF returns bytes, not words -
//              vanilla FreeRTOS returns words, which is an easy 4x mistake)
//    lv_mem  LVGL's own pool, which is separate from the heap and runs out first
//    flush   how much the panel is actually asked to repaint.  This is the
//              number that tells "my widget is slow" apart from "something is
//              invalidating far more area than I think" - and during this port
//              it was the only reason the real costs were ever found.
//    mode    the display mode, because none of the above means anything without
//              it: Fwd/Ref draws two power bars and costs ~25% more per second
//              than the single-bar modes, which is easily mistaken for a
//              regression when two captures are compared across a mode change.
//*********************************************************************************
static uint32_t statsFrameMaxUs = 0;
static uint32_t statsFrameSumUs = 0;
static uint32_t statsFrameCount = 0;
static uint32_t statsTickMinUs  = 0xFFFFFFFF;
static uint32_t statsTickMaxUs  = 0;
static uint32_t statsTickSumUs  = 0;
static uint32_t statsTickCount  = 0;

static void statsFrame(uint32_t us)
{
  if (us > statsFrameMaxUs) statsFrameMaxUs = us;
  statsFrameSumUs += us;
  statsFrameCount++;
}

static void statsTick()
{
  static uint32_t prev = 0;
  uint32_t now = micros();
  if (prev) {
    uint32_t dt = now - prev;
    if (dt < statsTickMinUs) statsTickMinUs = dt;
    if (dt > statsTickMaxUs) statsTickMaxUs = dt;
    statsTickSumUs += dt;
    statsTickCount++;
  }
  prev = now;
}

static void statsReport()
{
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 1000) return;
  last = now;

  Serial.printf("[stats] heap %u free / %u min / %u largest-dma | ui %u max / %u avg us | stack %u/%u B free\n",
                (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMinFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
                (unsigned)statsFrameMaxUs,
                (unsigned)(statsFrameCount ? statsFrameSumUs / statsFrameCount : 0),
                (unsigned)uxTaskGetStackHighWaterMark(uiTaskHandle),
                (unsigned)UI_TASK_STACK);

  lv_mem_monitor_t m;
  lv_mem_monitor(&m);
  Serial.printf("[stats] lv_mem %u%% used, %u%% frag, %u B largest free of %u\n",
                (unsigned)m.used_pct, (unsigned)m.frag_pct,
                (unsigned)m.free_biggest_size, (unsigned)m.total_size);

  LvglPort::FlushStats f = LvglPort::takeFlushStats();
  Serial.printf("[stats] flush %u calls/s, %u px/s, %u px worst\n",
                (unsigned)f.calls, (unsigned)f.pixels, (unsigned)f.maxPixels);

  Serial.printf("[stats] ui tick %u / %u / %u us (min/avg/max), nominal %u | mode %u\n",
                (unsigned)statsTickMinUs,
                (unsigned)(statsTickCount ? statsTickSumUs / statsTickCount : 0),
                (unsigned)statsTickMaxUs,
                (unsigned)(UI_TICK_MS * 1000),
                (unsigned)config.settings().modeDisplay);

  statsFrameMaxUs = 0;
  statsFrameSumUs = 0;
  statsFrameCount = 0;
  statsTickMinUs  = 0xFFFFFFFF;
  statsTickMaxUs  = 0;
  statsTickSumUs  = 0;
  statsTickCount  = 0;
}
#endif

//*********************************************************************************
//  UI TASK  -  core 1.  Owns the panel, the touch controller and the LEDs, and
//  never touches the detector hardware: it only reads the snapshot from core 0.
//*********************************************************************************
static void uiTask(void*)
{
  //  LVGL is initialised HERE rather than in setup(), so there is never a window
  //  in which two tasks could both be "the LVGL task".
  if (!lvgl.begin(display)) {
#if BRINGUP_STATS
    Serial.println("[lvgl] port init FAILED");
#endif
    vTaskDelete(nullptr);
  }

  UiTheme::begin(config.settings().theme);
  ui.begin(config, meter, display, display, new BootScreen());

  TickType_t lastWake = xTaskGetTickCount();

  for (;;)
  {
    meter.syncReadings();            // take the latest snapshot from core 0

    const MeterReadings& r = meter.readings();
    digitalWrite(LED_G, r.powerDetected ? LOW : HIGH);
    //  Driven both ways.  It used to only ever be switched ON, which was the
    //  visible half of an alarm that had no way to be cleared at all; now that
    //  a tap acknowledges it, the LED has to be able to follow it back off.
    digitalWrite(LED_R, r.swrAlarm ? LOW : HIGH);

#if BRINGUP_STATS
    statsTick();
    uint32_t t0 = micros();
#endif

    ui.tick(millis());             // deferred navigation + METER_TICK_MS pump
    lv_timer_handler();            // refresh + indev + LVGL timers
    lvgl.endFrame();               // drain the last overlapped DMA push

#if BRINGUP_STATS
    statsFrame(micros() - t0);
    statsReport();
#endif

    //  Delay UNTIL, not FOR.  vTaskDelay() here made the period UI_TICK_MS PLUS
    //  the frame time - so a nominal 5 ms pump actually ran at ~9.4 ms, right on
    //  top of LVGL's 10 ms refresh, which is precisely the beat UI_TICK_MS was
    //  chosen to sit below (see AppConfig.h).  It is also the exact defect the
    //  old TFT_eSPI firmware was measured to have; inheriting it was accidental.
    //
    //  The false branch is the guard this needs and vTaskDelay never did: once a
    //  frame overruns the period - a full-screen repaint is ~22 ms of SPI - the
    //  deadline is already past and xTaskDelayUntil returns WITHOUT blocking.
    //  Left alone that spins, never yields, and starves core 1's idle task into
    //  the task watchdog.  Resync and give up one tick instead.
    if (!xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(UI_TICK_MS))) {
      lastWake = xTaskGetTickCount();
      vTaskDelay(1);
    }
  }
}

void setup()
{
#if BRINGUP_STATS
  Serial.begin(115200);
#endif

  // CYD onboard RGB LED (active LOW) - off at boot.
  pinMode(LED_R, OUTPUT); digitalWrite(LED_R, HIGH);
  pinMode(LED_G, OUTPUT); digitalWrite(LED_G, HIGH);
  pinMode(LED_B, OUTPUT); digitalWrite(LED_B, HIGH);

  config.begin();                  // defaults + load/validate from NVS
  display.begin();                 // TFT panel + touch controller
  meter.begin(config.settings());  // detector probe + math, no task yet

  //  The splash is a screen with a deadline, not a delay(), so measurement can
  //  start immediately and its averaging windows fill behind it.
  meter.start();

  xTaskCreatePinnedToCore(uiTask, "ui", UI_TASK_STACK, nullptr,
                          UI_TASK_PRIO, &uiTaskHandle, UI_TASK_CORE);

  vTaskDelete(nullptr);            // the Arduino loop task has nothing left to do
}

void loop() {}                     // never runs - see the two pinned tasks above

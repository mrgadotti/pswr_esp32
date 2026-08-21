//*********************************************************************************
//  MeterEngine.cpp
//*********************************************************************************
#include "MeterEngine.h"

#include "AppConfig.h"
#include "MockDetector.h"
#include "Pins.h"

//  Compile-time ADC choice.  ADS1015 is the default - a normal build needs no
//  flag at all - so this block only matters when swapping it out: add
//  -D ADC_FIXED_VALUES to platformio.ini's build_flags to build against
//  FixedDetector instead (bring-up with no ADC wired yet, or the copy-paste
//  starting point for a new chip's driver - see FixedDetector.h).  A third ADC
//  adds its own #elif branch here; nothing below this block needs to change.
#ifndef ADC_FIXED_VALUES
#define ADC_ADS1X15
#endif

#ifdef ADC_ADS1X15
  #include "Ads1015Detector.h"
  using ActiveAdc = Ads1015Detector;
#else
  #include "FixedDetector.h"
  using ActiveAdc = FixedDetector;
#endif

//  The real front-ends, filled in by ActiveAdc::probeAll() below.  File-static
//  rather than members so this header stays free of the concrete detector
//  type - the #if block above is the only place that names one.
static RfDetector* gReal[ActiveAdc::ADDR_N] = { nullptr };

//  Points at the array of simulated couplers when nothing answered on the bus.
//  Indexed the same way gReal[] is, so selectCoupler() differs by one line.
static MockDetector* gMocks = nullptr;

//  How many sample cycles per SWR update.  The SWR average window (AVG_BUFSWR)
//  was tuned in the original firmware against the UI poll rate, so the divider
//  keeps that relationship rather than the literal number.
static constexpr uint32_t SWR_DIVIDER = (POLL_MS / SAMPLE_MS) ? (POLL_MS / SAMPLE_MS) : 1;

void MeterEngine::begin(Settings& settings)
{
  settings_ = &settings;

  //  Enumerate the bus once.  Probing costs one address-only transaction per
  //  candidate, and doing it here rather than lazily means the UI can be told
  //  how many couplers exist before it draws its first frame.
  couplerCount_ = ActiveAdc::probeAll(I2C_SDA, I2C_SCL, I2C_HZ, gReal);
  simulated_    = (couplerCount_ == 0);

  if (simulated_) {
    //  No ADS1015 fitted: simulate MOCK_COUPLERS of them, so the coupler
    //  selector and the per-coupler calibration stay exercisable rather than
    //  being the one path nobody can reach on the bench.  Constructed here
    //  rather than statically because they need live Settings.
    static_assert(MOCK_COUPLERS == 2, "add an initialiser per profile below");
    static MockDetector mocks[MOCK_COUPLERS] = {
      { settings, MOCK_PROFILES[0] },
      { settings, MOCK_PROFILES[1] },
    };

    gMocks = mocks;
    for (uint8_t i = 0; i < MOCK_COUPLERS; i++) mocks[i].begin();
    couplerCount_ = MOCK_COUPLERS;
  }

  //  A stored coupler number can outlive the coupler itself - the meter was
  //  calibrated with three fitted and now has one.  Clamp rather than fail, and
  //  write the clamp back so the UI and the engine agree from frame one.
  if (settings.coupler < 1 || settings.coupler > couplerCount_)
    settings.coupler = 1;

  selectCoupler(settings.coupler);
  math_.reset();
}

uint8_t MeterEngine::activeAddress() const
{
  //  Simulated couplers have no address, and reporting one would send whoever
  //  is chasing a missing coupler to inspect strapping that does not exist.
  //  Otherwise generic: whatever the active front-end's diagId() means is
  //  between it and the debug screen, this class does not need to know.
  if (simulated_) return 0;
  return detector_ ? detector_->diagId() : 0;
}

void MeterEngine::selectCoupler(uint8_t coupler)
{
  if (coupler < 1 || coupler > couplerCount_) coupler = 1;

  activeCoupler_ = coupler;
  detector_      = simulated_ ? (RfDetector*)&gMocks[coupler - 1]
                              : gReal[coupler - 1];

  //  The peak, PEP and averaging windows hold samples taken through the OTHER
  //  coupler, scaled by the other coupler's calibration.  Left in place they
  //  would decay out over a second or two while reading plausible nonsense, so
  //  they are dropped outright.
  math_.reset();
}

void MeterEngine::start()
{
  xTaskCreatePinnedToCore(taskTrampoline, "meter", METER_TASK_STACK, this,
                          METER_TASK_PRIO, nullptr, METER_TASK_CORE);
}

void MeterEngine::taskTrampoline(void* arg)
{
  static_cast<MeterEngine*>(arg)->taskLoop();
}

//*********************************************************************************
//  METER TASK  -  core 0.
//
//  The peak / PEP / averaging windows are counted in SAMPLES, not milliseconds,
//  so the math has to advance exactly once per acquisition.  That is the whole
//  reason this lives on its own core rather than in the UI loop, whose timing is
//  at the mercy of every redraw.
//
//  vTaskDelayUntil, not vTaskDelay: the period must be measured from the start
//  of the previous cycle, otherwise the ~1.2 ms the ADS1015 spends converting
//  would be added to every interval and the windows would silently stretch.
//
//  COUPLER CHANGES are picked up here rather than pushed in from the UI.  The
//  UI writes one byte (Settings::coupler) and this loop notices it on its next
//  cycle, so the swap - repointing the detector and clearing the windows -
//  happens between two samples on the core that owns them.  A setter called
//  from core 1 would be reaching into buffers this task is mid-way through.
//*********************************************************************************
void MeterEngine::taskLoop()
{
  TickType_t lastWake = xTaskGetTickCount();
  uint32_t   swrDiv   = 0;

  for (;;)
  {
    //  Load the coupler once: reading the atomic twice in one expression could
    //  see two different values if the UI wrote in between.
    if (couplerCount_) {
      const uint8_t c = settings_->coupler.load();
      if (c != activeCoupler_)
        selectCoupler(c);
    }

    //  Alarm acknowledgement, handled the same way and for the same reason: the
    //  latch lives in live_, which only this task may write.  Cleared BEFORE the
    //  sample, so a fault still present re-latches on this very cycle and the
    //  acknowledgement cannot hide a live problem for even one frame.
    if (clearAlarmReq_.load()) {
      clearAlarmReq_.store(false);
      live_.swrAlarm  = false;
    }

    float fwd, rev;
    detector_->read(fwd, rev);

    math_.addSample(fwd, rev, *settings_, live_);
    if (++swrDiv >= SWR_DIVIDER) { swrDiv = 0; math_.updateSwr(*settings_, live_); }

    publish();
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SAMPLE_MS));
  }
}

//*********************************************************************************
//  SNAPSHOT HANDOFF  -  the only state shared between the two cores.
//
//  A spinlock around a ~150 byte struct copy: single-digit microseconds with
//  interrupts masked on one core, which is cheaper than the alternatives and
//  guarantees the reader never sees a half-updated set of figures.
//*********************************************************************************
void MeterEngine::publish()
{
  portENTER_CRITICAL(&mux_);
  published_ = live_;
  portEXIT_CRITICAL(&mux_);
}

void MeterEngine::syncReadings()
{
  portENTER_CRITICAL(&mux_);
  readerReadings_ = published_;
  portEXIT_CRITICAL(&mux_);
}

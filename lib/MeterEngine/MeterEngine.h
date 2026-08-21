//*********************************************************************************
//  MeterEngine.h  -  The coupling between an RfDetector and PowerMath.
//
//  Deliberately thin, and deliberately the ONLY place that knows about all four
//  of: the detector, the math, FreeRTOS, and the fact that a UI exists on the
//  other core.  Everything project-specific lives here:
//
//      - which detector is fitted (probes for a real one, falls back to the mock)
//      - how often to sample, and on which core
//      - how results cross to the reader without tearing
//
//  Lift PowerMath into another project and you will write your own version of
//  this file - it is ~100 lines, and that is the point of the split.
//
//  THREADING.  The whole measurement pipeline runs in the meter task on core 0,
//  so the math keeps its exact SAMPLE_MS cadence no matter how long the UI
//  spends redrawing.  Results are copied into a published snapshot under a
//  spinlock; the reader takes its own copy through syncReadings() and then reads
//  it freely via readings(), so every value it sees belongs to one instant.
//*********************************************************************************
#pragma once

#include <Arduino.h>

#include <atomic>

#include "PowerMath.h"
#include "RfDetector.h"
#include "Types.h"

class MeterEngine {
public:
  //  Bind settings and bring the front end up.  Probes every ADS1015 address on
  //  the bus, so a meter with two couplers wired comes up with two available;
  //  if none answers it silently falls back to the simulator, and
  //  detectorPresent() is how a caller tells which one it got.
  //  Does NOT start the task yet; see start().
  void begin(Settings& settings);

  //  Launch the core-0 meter task.  Averaging windows fill from empty from this
  //  moment, so start it when the meter should begin measuring.
  void start();

  //  Reader side: refresh the local copy from the snapshot published by core 0.
  //  Call once per frame, before reading readings().
  void syncReadings();

  //  Acknowledge a latched SWR alarm.  Safe from the UI task, and from inside an
  //  LVGL event callback: it only raises a flag that the meter task acts on at
  //  the top of its next cycle, on the core that owns the readings - the same
  //  arrangement Settings::coupler uses, and for the same reason.
  //
  //  A request arriving in the window between the task testing the flag and
  //  clearing it is lost.  That is an acknowledgement gesture with the alarm
  //  still on screen, so the operator simply taps again; the alternative is a
  //  spinlock around a bool, which costs more than the failure does.
  void clearSwrAlarm() { clearAlarmReq_.store(true); }

  //  Reader-side view of the measurements, as of the last syncReadings().
  const MeterReadings& readings() const { return readerReadings_; }

  //  False when the readings are simulated.  The UI marks the display with
  //  this, so it must not lie.
  bool detectorPresent() const { return detector_ && detector_->present(); }

  //  How many couplers are selectable: the number of ADS1015 converters that
  //  answered at boot, or MOCK_COUPLERS when simulating.  Never zero.  The UI
  //  locks the coupler control when this is below 2, since offering a choice the
  //  hardware cannot honour would just select an uncalibrated set of numbers.
  //
  //  Pair it with detectorPresent() before saying anything about hardware - a
  //  count of 2 means two SIMULATED couplers when that returns false.
  uint8_t couplerCount() const { return couplerCount_; }

  //  I2C address behind the coupler currently selected, 0 when simulating.
  //  Shown on the debug screen so a coupler that never appears can be traced to
  //  its ADDR strapping.
  uint8_t activeAddress() const;

private:
  static void taskTrampoline(void* arg);
  void        taskLoop();
  void        publish();

  //  Meter-task side of a coupler change.  Repoints the detector and clears the
  //  averaging windows, because the samples already in them were taken through
  //  a different coupler with a different calibration.
  void selectCoupler(uint8_t coupler);

  Settings*   settings_ = nullptr;
  RfDetector* detector_ = nullptr;   // the active front end
  PowerMath   math_;

  //  Which of the probed converters is live.  Meter-task-only state: the UI
  //  changes Settings::coupler and the task notices, so there is no second
  //  source of truth and nothing to synchronise.
  uint8_t couplerCount_  = 0;
  uint8_t activeCoupler_ = 0;        // 1-based
  bool    simulated_     = false;    // couplers come from the mock, not the bus

  //  Set by the UI, consumed by the meter task.  atomic rather than volatile
  //  bool: it is the one field written on one core and read on the other outside
  //  the spinlock, and the atomic store/load is the portable form of what a
  //  volatile byte only happened to guarantee on this particular core.
  std::atomic<bool> clearAlarmReq_{false};

  MeterReadings live_;             // core 0 only: the running state
  MeterReadings published_;        // shared: guarded by mux_
  MeterReadings readerReadings_;   // reader core only

  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

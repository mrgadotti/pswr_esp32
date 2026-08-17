//*********************************************************************************
//  RfDetector.h  -  Contract between an RF detector front-end and the
//                   measurement math.
//
//  The whole measurement chain is deliberately split in three, so that either
//  half can be lifted into another project without dragging the other along:
//
//      RfDetector   what the hardware reads   (volts)      <- this file
//      PowerMath    what those volts mean     (W, SWR, dB) <- no HW, no RTOS
//      MeterEngine  the thin coupling         (task, snapshot, cadence)
//
//  A different project needing only the math links PowerMath and feeds it volts
//  from wherever.  A project needing only the front-end implements this
//  interface and ignores PowerMath.  MeterEngine is the ~100 lines that decide
//  how often, on which core, and how results cross to the UI - and that is the
//  part most likely to be rewritten per project.
//
//  Dependency-free on purpose: stdint only, so it can sit in include/ and be
//  implemented and consumed without either side pulling the other's headers.
//*********************************************************************************
#pragma once

#include <stdint.h>

struct RfDetector {
  virtual ~RfDetector() = default;

  //  Bring the front-end up.  Returns true if a real device answered; a
  //  simulator returns true as well - use present() to tell them apart.
  virtual bool begin() = 0;

  //  False when the reading is simulated rather than measured.  The UI uses
  //  this to mark the display, so it must not lie.
  virtual bool present() const = 0;

  //  One forward/reflected detector sample, in VOLTS at the detector output.
  //  Called from the measurement task at its sampling cadence, so it must be
  //  bounded and must not block on anything slow.
  virtual void read(float& fwdVolts, float& revVolts) = 0;

  //  A hardware identifier for diagnostics - the debug screen shows it so a
  //  front-end that never appears can be traced back to its wiring.  Meaning
  //  is up to the implementation (an I2C address, a chip-select pin, ...).
  //  Default is 0, "not applicable", which is also the right answer for a
  //  simulated front-end - no override needed there.
  virtual uint8_t diagId() const { return 0; }
};

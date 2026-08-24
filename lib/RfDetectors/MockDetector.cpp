//*********************************************************************************
//  MockDetector.cpp
//*********************************************************************************
#include "MockDetector.h"

#include <esp_random.h>
#include <math.h>

#include "AppConfig.h"

//  Uniform 0..1 from the hardware RNG.  Called at the sample rate, where
//  esp_random() is a register read and costs nothing worth measuring.
static inline double rand01()
{
  return (double)(esp_random() >> 8) / (double)(1UL << 24);
}

bool MockDetector::begin()
{
  logWatts_ = log10(p_->centreW);
  swr_      = 0.5 * (p_->minSwr + p_->maxSwr);

  //  Size the SWR step so the walk really covers the profile's band rather than
  //  hovering in the middle of it.  This is an Ornstein-Uhlenbeck process in
  //  disguise: with uniform steps in +/-s and reversion k, the stationary sigma
  //  is (s/sqrt(3)) / sqrt(2k).  Asking for the band edges at 2 sigma inverts to
  //  the line below.  Derived rather than tabulated because otherwise every
  //  change to a profile's band would silently leave the step behind, and the
  //  symptom - an SWR that never reaches the number in the table - is the kind
  //  nobody notices.
  swrStep_ = (p_->maxSwr - p_->minSwr) / 4.0 * sqrt(3.0) * sqrt(2.0 * MOCK_SWR_REVERT);
  return true;
}

void MockDetector::read(float& fwdVolts, float& revVolts)
{
  //  ---- advance the walk ----------------------------------------------------
  logWatts_ += (rand01() - 0.5) * 2.0 * MOCK_STEP_DB;
  logWatts_ += (log10(p_->centreW) - logWatts_) * MOCK_REVERT;

  double watts = pow(10.0, logWatts_);
  if (watts < p_->minW) { watts = p_->minW; logWatts_ = log10(watts); }
  if (watts > p_->maxW) { watts = p_->maxW; logWatts_ = log10(watts); }

  swr_ += (rand01() - 0.5) * 2.0 * swrStep_;
  swr_ += (0.5 * (p_->minSwr + p_->maxSwr) - swr_) * MOCK_SWR_REVERT;
  if (swr_ < p_->minSwr) swr_ = p_->minSwr;
  if (swr_ > p_->maxSwr) swr_ = p_->maxSwr;

  //  ---- invert the measurement math to get detector volts -------------------
  const double rho   = (swr_ - 1.0) / (swr_ + 1.0);
  const double fwdMw = watts * 1000.0;
  const double refMw = fwdMw * rho * rho;

  //  Inverted through the SELECTED coupler's calibration, so switching couplers
  //  on a meter with no hardware still exercises the path end to end.
  const Calibration& c = s_->activeCal();

  if (c.detector == DetectorType::AD8307)
  {
    //  Undo the two-point log-amp fit: volts = V1 + (dBm - dBm1) * slope.
    //
    //  Per channel, because each carries its own pair of reference levels since
    //  a reverse-only calibration can move one without the other - see the note
    //  on CalPoint in Types.h.  Inverting through a shared anchor would have the
    //  mock feed the reverse channel volts the real math no longer maps back to
    //  the SWR that was asked for, i.e. the simulator would quietly stop
    //  simulating the thing under test.
    const double fdbm = 10.0 * log10(fwdMw);
    const double rdbm = 10.0 * log10(refMw);
    const double db0F = c.calAd[0].db10m    / 10.0;
    const double db0R = c.calAd[0].revDb10m / 10.0;
    const double ddbF = (c.calAd[1].db10m    - c.calAd[0].db10m)    / 10.0;
    const double ddbR = (c.calAd[1].revDb10m - c.calAd[0].revDb10m) / 10.0;

    //  Both points at the same level is a legitimate half-finished calibration
    //  (SET P1 and SET P2 are separate presses), and dividing by that span gives
    //  an inf that reaches PowerMath as a detector voltage.  Fall back to the
    //  AD8307's nominal slope - the reciprocal of the NOMINAL_DB_PER_V that
    //  determineDbm() falls back to on the same input, so the two stay inverses
    //  of each other even when the stored fit is degenerate.
    const double NOMINAL_V_PER_DB = LOGAMP_SLOPE * 0.001;

    const double slopeF = (ddbF != 0.0) ? (c.calAd[1].fwd - c.calAd[0].fwd) / ddbF
                                        : NOMINAL_V_PER_DB;
    const double slopeR = (ddbR != 0.0) ? (c.calAd[1].rev - c.calAd[0].rev) / ddbR
                                        : NOMINAL_V_PER_DB;

    fwdVolts = c.calAd[0].fwd + (fdbm - db0F) * slopeF;
    revVolts = c.calAd[0].rev + (rdbm - db0R) * slopeR;
  }
  else
  {
    //  Undo the coupler ratio and the diode drop.
    const double vrmsF = sqrt(fwdMw * 50.0 / 1000.0);       // volts on the line
    const double vrmsR = vrmsF * rho;
    const double cf    = vrmsF / (c.bridgeCoupling * c.meterCal);   // at the detector
    const double cr    = vrmsR / (c.bridgeCoupling * c.meterCal);

    fwdVolts = (cf - D_VDROP) * 1.41421356 + D_VDROP;
    revVolts = (cr - D_VDROP) * 1.41421356 + D_VDROP;
    if (fwdVolts < 0) fwdVolts = 0;
    if (revVolts < 0) revVolts = 0;
  }
}

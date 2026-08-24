//*********************************************************************************
//  PowerMath.cpp  -  faithful port of the math in PSWRsamplerBuf.ino.
//
//  The only changes from the original are structural: the function-local statics
//  became members, and the working MeterReadings is passed in rather than being
//  a member of the ADC class.  Every expression, constant, clamp and rounding
//  step is unchanged - a real meter's stored calibration depends on them.
//*********************************************************************************
#include "PowerMath.h"

#include <math.h>

static inline double sqr(double x) { return x * x; }

//  Empty-window sentinel for the peak and PEP holds, in centi-dB.  It matches
//  the floor netPowerDb itself uses.  Filling with 0 instead - as the original
//  did - means 0 dB, i.e. 1 mW: a meter sitting with no carrier reported 1.0 mW
//  of peak and PEP until the windows filled, and after a PEP window change it
//  did so again for a whole window.
static constexpr int32_t HOLD_FLOOR_CDB = -9000;      // -90.0 dB

void PowerMath::reset()
{
  for (uint16_t i = 0; i < BUF_SHORT; i++)      peakBuf_[i]  = HOLD_FLOOR_CDB;
  for (uint16_t i = 0; i < PEP_MAX_WINDOW; i++) pepBuf_[i]   = HOLD_FLOOR_CDB;
  for (uint16_t i = 0; i < AVG_BUFSHORT; i++)   avgBuf_[i]   = 0;
  for (uint16_t i = 0; i < AVG_BUF1S; i++)      avg1sBuf_[i] = 0;
  for (uint16_t i = 0; i < AVG_BUFSWR; i++)     swrBuf_[i]   = 0;

  peakIdx_ = pepIdx_ = avgIdx_ = avg1sIdx_ = swrIdx_ = 0;
  avgAccum_ = avg1sAccum_ = swrAccum_ = 0;
  pepWin_   = 0;                                 // force a re-arm on next sample
}

//*********************************************************************************
//  AD8307 two-point dBm conversion.
//
//  Note the swap: when reflected reads higher than forward, the two are
//  exchanged and `reverse` is set, so everything downstream always treats
//  "forward" as the larger of the pair.  That is how the meter copes with being
//  wired backwards, and it is why fInst/rInst are not simply Fwd/Rev.
//*********************************************************************************
void PowerMath::determineDbm(const Settings& s, MeterReadings& out)
{
  //  activeCal(), not a fixed set: each coupler carries its own two-point fit.
  const Calibration& c = s.activeCal();

  //  ONE FIT PER CHANNEL, each with its own pair of reference levels.  The
  //  Calibrate screen can store one channel without the other - feeding the
  //  coupler backwards measures `rev` alone - and while these anchors were
  //  shared, doing that moved the forward fit's x-axis without moving its
  //  stored volts.  See the note on CalPoint in Types.h.
  const double delta_Fdb_span = (double)(c.calAd[1].db10m    - c.calAd[0].db10m)    / 10.0;
  const double delta_Rdb_span = (double)(c.calAd[1].revDb10m - c.calAd[0].revDb10m) / 10.0;

  //  DEGENERATE CALIBRATION.  Nothing guarantees the two points sit at different
  //  voltages: the Calibrate screen warns but still stores, SET P1 / SET P2 are
  //  separate presses so the pair is legitimately half-updated in between, and
  //  its "Rev only" path never touches the forward pair at all - so a coupler
  //  can carry a flat fwd fit for a whole session.
  //
  //  Dividing by that span gives inf, and one inf here does not stay local: it
  //  becomes a NaN netPowerDb, and the peak and PEP windows are MAX scans, so
  //  they latch it and the meter reads nonsense until it is power-cycled.
  //  Fall back to the AD8307's nominal slope instead - wrong by whatever the
  //  calibration would have corrected, but bounded, monotonic, and still
  //  tracking the carrier.
  const double NOMINAL_DB_PER_V = 1000.0 / LOGAMP_SLOPE;

  const double spanF = (double)c.calAd[1].fwd - c.calAd[0].fwd;
  const double spanR = (double)c.calAd[1].rev - c.calAd[0].rev;

  const double delta_Fdb = (fabs(spanF) < CAL_MIN_SPAN_V) ? NOMINAL_DB_PER_V
                                                          : delta_Fdb_span / spanF;
  const double delta_Rdb = (fabs(spanR) < CAL_MIN_SPAN_V) ? NOMINAL_DB_PER_V
                                                          : delta_Rdb_span / spanR;

  out.ad8307FwdDbm = (out.vFwd - c.calAd[0].fwd) * delta_Fdb + c.calAd[0].db10m    / 10.0;
  out.ad8307RevDbm = (out.vRev - c.calAd[0].rev) * delta_Rdb + c.calAd[0].revDb10m / 10.0;

  if (out.ad8307FwdDbm >= out.ad8307RevDbm) {
    out.reverse = false;
  } else {
    double t = out.ad8307RevDbm;
    out.ad8307RevDbm = out.ad8307FwdDbm;
    out.ad8307FwdDbm = t;
    out.reverse = true;
  }
}

void PowerMath::addSample(float fwdVolts, float revVolts,
                          const Settings& s, MeterReadings& out)
{
  out.vFwd = fwdVolts;
  out.vRev = revVolts;

  //  activeCal(), not s.detector: which front end is fitted is itself a
  //  per-coupler property now, so it has to be read from the same place the
  //  two-point fit and meterCal already come from.
  const Calibration& c = s.activeCal();

  if (c.detector == DetectorType::AD8307)
  {
    determineDbm(s, out);
    out.fInst      = pow(10, out.ad8307FwdDbm / 20.0);
    out.fwdPowerMw = sqr(out.fInst);
    out.rInst      = pow(10, out.ad8307RevDbm / 20.0);
    out.refPowerMw = sqr(out.rInst);
  }
  else
  {
    //  Diode / Bruene bridge: undo the detector's diode drop and the coupler
    //  ratio to get back to line volts, then P = V^2 / 50.
    const float mcal = c.meterCal;

    float fv = out.vFwd, rv = out.vRev;
    if (fv >= rv) { out.reverse = false; }
    else          { float t = rv; rv = fv; fv = t; out.reverse = true; }

    out.fInst = fv;
    if (out.fInst >= D_VDROP) out.fInst = (out.fInst - D_VDROP) / 1.41421356 + D_VDROP;
    out.fInst = out.fInst * c.bridgeCoupling * mcal;
    out.fwdPowerMw = 1000.0 * sqr(out.fInst) / 50.0;

    out.rInst = rv;
    if (out.rInst >= D_VDROP) out.rInst = (out.rInst - D_VDROP) / 1.41421356 + D_VDROP;
    out.rInst = out.rInst * c.bridgeCoupling * mcal;
    out.refPowerMw = 1000.0 * sqr(out.rInst) / 50.0;
  }

  //  9 kW ceiling, from the original: keeps a wild reading from poisoning the
  //  peak and average windows for the next second.
  if (out.fwdPowerMw > 9000000) out.fwdPowerMw = 9000000;
  if (out.refPowerMw > 9000000) out.refPowerMw = 9000000;

  out.netPowerMw = out.fwdPowerMw - out.refPowerMw;
  if (out.netPowerMw < 0) out.netPowerMw = -out.netPowerMw;
  out.netPowerDb = (out.netPowerMw > 1e-9) ? 10.0 * log10(out.netPowerMw) : -90.0;

  //  ---- peak hold over BUF_SHORT samples -----------------------------------
  peakBuf_[peakIdx_++] = (int32_t)(100 * out.netPowerDb);
  if (peakIdx_ == BUF_SHORT) peakIdx_ = 0;

  int32_t pk = -0x7fffffff;
  for (uint16_t x = 0; x < BUF_SHORT; x++) if (pk < peakBuf_[x]) pk = peakBuf_[x];
  out.peakPowerDb = pk / 100.0;

  //  ---- PEP hold, advanced once per completed peak window ------------------
  if (peakIdx_ == 0)
  {
    const uint16_t win = PEP_OPTIONS[s.pepIdx % 3];

    //  The window changed under us.  pepBuf_ still holds entries written when it
    //  was a different size, and the scan below is a MAX over the FIRST `win`
    //  slots - so widening 100 ms -> 2.5 s exposes slots 1..24, which were last
    //  written whenever the window was 25 and can be minutes stale.  The old
    //  peak comes back from the dead and is held for a full window.  Narrowing
    //  is the same bug pointed the other way: slot 0 is read before it has been
    //  written under the new window.  Drop the lot and start the window clean.
    if (win != pepWin_) {
      for (uint16_t i = 0; i < PEP_MAX_WINDOW; i++) pepBuf_[i] = HOLD_FLOOR_CDB;
      pepIdx_ = 0;
      pepWin_ = win;
    }

    pepBuf_[pepIdx_++] = (int32_t)(100 * out.peakPowerDb);
    if (pepIdx_ >= win) pepIdx_ = 0;

    pk = -0x7fffffff;
    for (uint16_t x = 0; x < win; x++) if (pk < pepBuf_[x]) pk = pepBuf_[x];
    out.pepPowerDb = pk / 100.0;
  }
  if (out.pepPowerDb < out.peakPowerDb) out.pepPowerDb = out.peakPowerDb;

  //  ---- two sliding-sum averages -------------------------------------------
  //  Subtracting the slot about to be overwritten is what makes these O(1)
  //  rather than a scan; the divisor is N-1 because the newest sample has been
  //  added but its predecessor has already been removed.
  avgBuf_[avgIdx_++] = out.netPowerMw;
  avgAccum_ += out.netPowerMw;
  if (avgIdx_ == AVG_BUFSHORT) avgIdx_ = 0;
  avgAccum_ -= avgBuf_[avgIdx_];
  out.avgPowerMw = avgAccum_ / (AVG_BUFSHORT - 1);

  avg1sBuf_[avg1sIdx_++] = out.netPowerMw;
  avg1sAccum_ += out.netPowerMw;
  if (avg1sIdx_ == AVG_BUF1S) avg1sIdx_ = 0;
  avg1sAccum_ -= avg1sBuf_[avg1sIdx_];
  out.avg1sPowerMw = avg1sAccum_ / (AVG_BUF1S - 1);
}

void PowerMath::updateSwr(const Settings& s, MeterReadings& out)
{
  const double minCalc = (s.activeCal().detector == DetectorType::AD8307)
                             ? MIN_PWR_FOR_SWR_CALC_AD8307
                             : MIN_PWR_FOR_SWR_CALC_DIODE;

  out.peakPowerMw = pow(10, out.peakPowerDb / 10.0);
  out.pepPowerMw  = pow(10, out.pepPowerDb / 10.0);

  //  Below minCalc the reflection coefficient is noise, so SWR is left holding
  //  its previous value rather than swinging wildly with no carrier.
  //
  //  GATED ON FORWARD POWER, NOT NET.  The reference firmware gated on
  //  net = |fwd - ref|, and that is backwards: net shrinks as the SWR rises and
  //  goes to zero as rho -> 1, so a net gate is hardest to clear at exactly the
  //  moment the answer matters.  Worked through at 30 mW forward on the AD8307
  //  (minCalc = 20 mW):
  //
  //      SWR 1.1 -> rho 0.048, ref  0.07 mW, net 29.9 mW  -> updates
  //      SWR 10  -> rho 0.818, ref 20.0  mW, net 10.0 mW  -> does NOT update
  //
  //  i.e. a flat load was measured and a badly mismatched one silently held the
  //  last good reading - 1.00 straight out of boot.  Forward power answers the
  //  question actually being asked, "is there a carrier here", and answers it
  //  the same way whatever the load is doing.
  //
  //  This is a DELIBERATE departure from the reference: it does not change any
  //  reading the old code produced, only whether a reading is produced at all.
  if (out.fwdPowerMw > minCalc)
  {
    double rho = out.rInst / out.fInst;
    if (rho > 0.999) rho = 0.999;          // clamp: rho = 1 is infinite SWR
    out.swr = (1 + rho) / (1 - rho);

    //  swrAlarmTrig == 40 means "off".  The alarm LATCHES; MeterEngine's
    //  clearSwrAlarm() is the one way out, wired to a tap on the meter face.
    //  Latching is deliberate - a brief flash during a tune-up should still be
    //  visible after the fact - but the same net-vs-forward argument applies
    //  with more force here: an alarm whose power threshold got harder to reach
    //  as the SWR got worse is an alarm that stays quiet during the fault it
    //  exists to report.
    if ((s.swrAlarmTrig != 40) && ((out.swr * 10) >= s.swrAlarmTrig) &&
        (out.fwdPowerMw > s.swrAlarmPwrThresh))
      out.swrAlarm = true;
  }
  out.powerDetected = (out.fwdPowerMw > minCalc);

  swrBuf_[swrIdx_++] = out.swr;
  swrAccum_ += out.swr;
  if (swrIdx_ == AVG_BUFSWR) swrIdx_ = 0;
  swrAccum_ -= swrBuf_[swrIdx_];
  out.swrAvg = swrAccum_ / (AVG_BUFSWR - 1);
}

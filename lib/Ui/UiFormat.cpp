//*********************************************************************************
//  UiFormat.cpp  -  faithful port of PSWRprintFunc.ino / scale_ranges.
//*********************************************************************************
#include "UiFormat.h"

#include <stdio.h>
#include <string.h>

namespace UiFormat {

static const char* const UNITS[8] = { "pW", "nW", "uW", "mW", "W", "kW", "MW", "GW" };

void swr(char* out, size_t n, double s)
{
  //  The x100 split into whole/tenths/hundredths is only ever used while s < 10,
  //  where s * 100 fits a uint16_t without wrapping - so it lives inside those
  //  branches rather than being computed (and truncated) at the top for every s.
  //  The %u arguments are cast: uint16_t promotes to int, not unsigned int.
  if (s < 2.0) {
    const uint16_t sub = (uint16_t)(s * 100);
    snprintf(out, n, "%u.%02u", (unsigned)(sub / 100), (unsigned)(sub % 100));
  } else if (s <= 10.0) {
    const uint16_t sub = (uint16_t)(s * 100);
    snprintf(out, n, "%u.%01u", (unsigned)(sub / 100), (unsigned)((sub % 100) / 10));
  } else if (s <= 1000) {
    snprintf(out, n, "%4u", (unsigned)s);
  } else {
    snprintf(out, n, "9999");
  }
}

void dbm(char* out, size_t n, int16_t db10m)
{
  //  int32_t, not int16_t: -db10m is negated as an int, but assigning INT16_MIN
  //  back into an int16_t would overflow.  Unreachable at the real range
  //  (-100..530), but the wide type costs nothing and removes the footgun.
  int32_t t = (db10m < 0) ? -(int32_t)db10m : db10m;
  int32_t w = t / 10;
  t = t % 10;
  if (db10m < 0) snprintf(out, n, "-%u.%udBm", (unsigned)w, (unsigned)t);
  else           snprintf(out, n, "%u.%udBm",  (unsigned)w, (unsigned)t);
}

//*********************************************************************************
//  POWER  -  one decimal, two units, no ranging beyond them.
//
//  This is the one formatter that DEPARTS from the original, deliberately.  The
//  legacy rule was three significant figures across the whole pW..GW ladder,
//  which reads well frozen and badly in motion: a carrier drifting through
//  1.50W - 10.5W - 100W - 1.50kW moves its decimal point at every decade and
//  changes unit at the top, so the reading appears to jump while the power is
//  barely moving.
//
//  mW and W only.  This meter has no kW range - a 2 kW ceiling reads "2000W" -
//  and no sub-mW one, which is the one thing given up here: an AD8307 can
//  resolve far below a milliwatt, and all of that now reads "0.0mW".  On an
//  instrument whose smallest bar range is 11 mW that was never legible anyway.
//
//  The widest string this can produce is "999.9mW".  MeterScreen anchors its
//  readouts from that, so widening the format means moving them too.
//*********************************************************************************
void powerMw(char* out, size_t n, double pwr)
{
  double p = (pwr < 0) ? -pwr : pwr;

  if (p < 999.95) { snprintf(out, n, "%.1fmW", p); return; }

  p /= 1000.0;
  if (p >= 999.95) snprintf(out, n, "%uW", (unsigned)(p + 0.5));
  else             snprintf(out, n, "%.1fW", p);
}

void calPoint(char* out, size_t n, const char* tag, const CalPoint& p)
{
  if (p.db10m == p.revDb10m)
    snprintf(out, n, "%s: %+.1f dBm  F=%.3f R=%.3f",
             tag, p.db10m / 10.0, p.fwd, p.rev);
  else
    snprintf(out, n, "%s: F%+.1f R%+.1f  F=%.3f R=%.3f",
             tag, p.db10m / 10.0, p.revDb10m / 10.0, p.fwd, p.rev);
}

void scalePowerMeter(double fs, double* outFs, char* range)
{
  int8_t r = 3;
  while (fs < 1.0   && r > 0) { fs *= 1000; r--; }
  while (fs >= 1000 && r < 7) { fs /= 1000; r++; }
  *outFs = fs;
  strcpy(range, UNITS[r]);
}

double AutoScale::push(double pMw, const Settings& s)
{
  double decade = (s.detector == DetectorType::AD8307) ? 1.0 : 10000.0;

  buf_[idx_++] = pMw * 1000.0;                   // uW
  if (idx_ == SAMPLES) idx_ = 0;

  double max = 0;
  for (uint16_t i = 0; i < SAMPLES; i++) if (max < buf_[i]) max = buf_[i];

  while ((decade * s.scaleRange[2]) < max) decade *= 10;

  double scale;
  if      (max >= decade * s.scaleRange[1]) scale = decade * s.scaleRange[2];
  else if (max >= decade * s.scaleRange[0]) scale = decade * s.scaleRange[1];
  else                                      scale = decade * s.scaleRange[0];

  return scale / 1000.0;                         // mW
}

}  // namespace UiFormat

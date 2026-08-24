//*********************************************************************************
//  test_powermath.cpp  -  Host tests for the measurement math.
//
//  These exist for one reason: the per-coupler calibration added in v1.00 routes
//  every reading through Settings::activeCal(), and on a meter with a single
//  ADS1015 fitted that routing is NEVER EXERCISED - the coupler control is
//  locked and only cal[0] is ever selected.  A bench test cannot reach the
//  second coupler's path; this can.
//
//  The expected values below are hand-computed from the AD8307 two-point fit and
//  the diode bridge formula, not captured from a previous run, so they also fix
//  the arithmetic itself rather than merely detecting that it changed.
//
//      pio test -e native
//*********************************************************************************
#include <unity.h>

#include <math.h>

#include "AppConfig.h"
#include "PowerMath.h"
#include "Types.h"

//*********************************************************************************
//  FIXTURE
//*********************************************************************************

//  A Settings with the compile-time calibration in EVERY coupler, so a test only
//  has to change the coupler it cares about.  Fills a caller-owned struct rather
//  than returning one by value: Settings is no longer copyable since coupler
//  became std::atomic, and it is initialised field-by-field rather than memset -
//  memset over an atomic is not a valid way to initialise it.
static void makeSettings(Settings& s, DetectorType det)
{
  for (uint8_t i = 0; i < MAX_COUPLERS; i++) {
    s.cal[i].detector       = det;
    s.cal[i].calAd[0]       = { CAL1_NOR_VALUE, CAL1_NOR_VALUE, CALFWD1_DEFAULT, CALREV1_DEFAULT };
    s.cal[i].calAd[1]       = { CAL2_NOR_VALUE, CAL2_NOR_VALUE, CALFWD2_DEFAULT, CALREV2_DEFAULT };
    s.cal[i].meterCal       = METER_CAL;
    s.cal[i].bridgeCoupling = BRIDGE_COUPLING;
  }

  s.coupler           = 1;
  s.pepIdx            = 0;
  s.swrAlarmTrig      = SWR_ALARM_DEFAULT;
  s.swrAlarmPwrThresh = SWR_THRESHOLD_DEFAULT;

  //  The remaining fields are not read by PowerMath, but they are still written
  //  so no test ever observes an indeterminate value.
  s.scaleRange[0] = SCALE_RANGE1;
  s.scaleRange[1] = SCALE_RANGE2;
  s.scaleRange[2] = SCALE_RANGE3;
  s.modeDisplay   = DisplayMode::PowerBarPep;
  s.band          = 0;
  s.touchMinX = s.touchMaxX = 0;
  s.touchMinY = s.touchMaxY = 0;
  s.theme      = THEME_DEFAULT;
  s.brightness = BRIGHTNESS_DEFAULT;
}

//  One acquisition through a fresh PowerMath, which is what isolates the
//  instantaneous conversion from the peak/PEP/averaging windows.
static MeterReadings measure(const Settings& s, float vFwd, float vRev)
{
  PowerMath     math;
  MeterReadings r;
  math.reset();
  math.addSample(vFwd, vRev, s, r);
  math.updateSwr(s, r);
  return r;
}

void setUp() {}
void tearDown() {}

//*********************************************************************************
//  THE ANCHOR  -  the two-point fit itself, unchanged by the refactor.
//
//  With the default calibration, P1 is 40.0 dBm at 2.233 V.  Feeding exactly
//  that voltage must therefore give exactly 40 dBm, i.e. 10 W - which is a value
//  that can be checked by inspection rather than by trusting the code.
//*********************************************************************************
static void test_ad8307_reads_cal_point_exactly()
{
  Settings      s;
  makeSettings(s, DetectorType::AD8307);
  MeterReadings r = measure(s, CALFWD1_DEFAULT, CALREV2_DEFAULT);

  TEST_ASSERT_DOUBLE_WITHIN(0.001, 40.0, r.ad8307FwdDbm);   // P1 = 400 -> 40.0 dBm
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 10.0, r.ad8307RevDbm);   // P2 = 100 -> 10.0 dBm

  TEST_ASSERT_DOUBLE_WITHIN(0.1, 10000.0, r.fwdPowerMw);    // 40 dBm = 10 W
  TEST_ASSERT_DOUBLE_WITHIN(0.01,   10.0, r.refPowerMw);    // 10 dBm = 10 mW
  TEST_ASSERT_FALSE(r.reverse);
}

//*********************************************************************************
//  THE POINT OF THE WHOLE CHANGE  -  identical volts, different coupler,
//  different answer.
//
//  Coupler 2 is given a fit shifted 10 dB down, so the same 2.233 V has to read
//  30 dBm instead of 40 - exactly one tenth of the power.  If activeCal() were
//  ignored anywhere in the chain, this ratio would come out as 1.
//*********************************************************************************
static void test_ad8307_coupler_selects_its_own_calibration()
{
  Settings s;
  makeSettings(s, DetectorType::AD8307);

  //  Coupler 2: same detector volts, levels 10 dB lower.
  s.cal[1].calAd[0] = { CAL1_NOR_VALUE - 100, CAL1_NOR_VALUE - 100,
                        CALFWD1_DEFAULT, CALREV1_DEFAULT };
  s.cal[1].calAd[1] = { CAL2_NOR_VALUE - 100, CAL2_NOR_VALUE - 100,
                        CALFWD2_DEFAULT, CALREV2_DEFAULT };

  s.coupler = 1;
  const double p1 = measure(s, CALFWD1_DEFAULT, CALREV2_DEFAULT).fwdPowerMw;

  s.coupler = 2;
  const MeterReadings r2 = measure(s, CALFWD1_DEFAULT, CALREV2_DEFAULT);

  TEST_ASSERT_DOUBLE_WITHIN(0.001, 30.0, r2.ad8307FwdDbm);
  TEST_ASSERT_DOUBLE_WITHIN(0.1,  1000.0, r2.fwdPowerMw);
  TEST_ASSERT_DOUBLE_WITHIN(0.01,   10.0, p1 / r2.fwdPowerMw);   // 10 dB = 10x
}

//*********************************************************************************
//  Same again on the diode path, which reaches activeCal() through meterCal
//  rather than through the two-point fit - a separate branch, separately wrong.
//  Power goes as the square of meterCal, so doubling it must quadruple the
//  reading.
//*********************************************************************************
static void test_diode_coupler_selects_its_own_meter_cal()
{
  Settings s;
  makeSettings(s, DetectorType::Diode);
  s.cal[1].meterCal = 2.0f * METER_CAL;

  s.coupler = 1;
  const double p1 = measure(s, 1.25f, 0.30f).fwdPowerMw;

  s.coupler = 2;
  const double p2 = measure(s, 1.25f, 0.30f).fwdPowerMw;

  //  Hand-computed for meterCal = 1.0:
  //      v = (1.25 - 0.25)/sqrt(2) + 0.25 = 0.957107
  //      V = v * 24 = 22.9706  ->  P = 1000 * V^2 / 50 = 10552.9 mW
  TEST_ASSERT_DOUBLE_WITHIN(1.0, 10552.9, p1);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 4.0, p2 / p1);
}

//*********************************************************************************
//  couplerIdx() is the guard that keeps a stale NVS value from indexing off the
//  end of cal[].  A meter calibrated with four couplers and reflashed with a
//  build allowing fewer must not read out of bounds - it must fall back to 1.
//*********************************************************************************
static void test_coupler_index_is_clamped()
{
  Settings s;
  makeSettings(s, DetectorType::AD8307);

  s.coupler = 0;                TEST_ASSERT_EQUAL_UINT8(0, s.couplerIdx());
  s.coupler = 1;                TEST_ASSERT_EQUAL_UINT8(0, s.couplerIdx());
  s.coupler = MAX_COUPLERS;     TEST_ASSERT_EQUAL_UINT8(MAX_COUPLERS - 1, s.couplerIdx());
  s.coupler = MAX_COUPLERS + 1; TEST_ASSERT_EQUAL_UINT8(0, s.couplerIdx());
  s.coupler = 255;              TEST_ASSERT_EQUAL_UINT8(0, s.couplerIdx());
}

//*********************************************************************************
//  The reverse swap: wired backwards, the meter must exchange the two channels
//  and say so, rather than reporting an SWR computed the wrong way round.
//*********************************************************************************
static void test_reverse_swaps_channels()
{
  Settings      s;
  makeSettings(s, DetectorType::AD8307);
  MeterReadings r = measure(s, CALREV2_DEFAULT, CALFWD1_DEFAULT);   // rev > fwd

  TEST_ASSERT_TRUE(r.reverse);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 40.0, r.ad8307FwdDbm);   // the larger one
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 10.0, r.ad8307RevDbm);
}

//*********************************************************************************
//  REVERSE-ONLY CALIBRATION MUST NOT MOVE THE FORWARD FIT.
//
//  This is the regression for the shared-anchor defect.  Feeding the coupler
//  BACKWARDS is how the reverse detector gets calibrated on its own hardware,
//  and on that path the Calibrate screen stores `rev` and nothing else.  While
//  one db10m served both channels, that store also moved the FORWARD fit's
//  reference level while leaving its stored volts alone - so the forward reading
//  shifted by the whole gap between the two Ref levels, silently.
//
//  Reproduced below at the numbers that make the size of it obvious: the meter's
//  forward pair sits at 40 dBm, the operator calibrates the reverse channel at
//  20 dBm, and the forward channel must still read 40 dBm at its own P1 voltage.
//  Before the fix it read 20 dBm - one hundredth of the power, with nothing on
//  screen to say so.
//*********************************************************************************
static void test_reverse_only_calibration_leaves_forward_fit_alone()
{
  Settings s;
  makeSettings(s, DetectorType::AD8307);

  //  A weak reverse signal, so nothing here trips the fwd/rev swap and the
  //  assertions below are about the channels they name.
  const float V_REV_QUIET = CALFWD2_DEFAULT;

  const MeterReadings before = measure(s, CALFWD1_DEFAULT, V_REV_QUIET);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 40.0, before.ad8307FwdDbm);

  //  What "SET P1" now writes with the coupler fed backwards: the reverse
  //  anchor and the reverse volts, and NOTHING else.  db10m and fwd are
  //  deliberately untouched - they describe a forward measurement this press
  //  never made.
  s.cal[0].calAd[0].revDb10m = 200;      // 20.0 dBm
  s.cal[0].calAd[0].rev      = 1.800f;

  const MeterReadings after = measure(s, CALFWD1_DEFAULT, 1.800f);

  //  The forward fit is untouched: same dBm, same power, to the bit.
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 40.0, after.ad8307FwdDbm);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, before.ad8307FwdDbm, after.ad8307FwdDbm);
  TEST_ASSERT_DOUBLE_WITHIN(0.1, before.fwdPowerMw, after.fwdPowerMw);
  TEST_ASSERT_FALSE(after.reverse);

  //  And the reverse channel now reads through its OWN anchor: fed exactly the
  //  volts just stored, it must report exactly the level they were stored at.
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 20.0, after.ad8307RevDbm);
}

//*********************************************************************************
//  The mirror of the test above: a FORWARD calibration still moves both anchors,
//  because with the coupler fed forwards the screen copies the measured forward
//  volts into both channels (the reverse detector is assumed identical).  Split
//  anchors must not turn that ordinary case into two divergent fits.
//*********************************************************************************
static void test_forward_calibration_moves_both_anchors()
{
  Settings s;
  makeSettings(s, DetectorType::AD8307);

  //  What "SET P1" writes with FWD OK, at a Ref of 30 dBm.
  s.cal[0].calAd[0].db10m    = 300;
  s.cal[0].calAd[0].revDb10m = 300;
  s.cal[0].calAd[0].fwd      = 2.000f;
  s.cal[0].calAd[0].rev      = 2.000f;

  const MeterReadings r = measure(s, 2.000f, 2.000f);

  TEST_ASSERT_DOUBLE_WITHIN(0.001, 30.0, r.ad8307FwdDbm);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 30.0, r.ad8307RevDbm);
}

//*********************************************************************************
//  SWR from a known reflection coefficient.  100 mV_rms forward against
//  3.16228 reflected gives rho = 0.0316228 and SWR = 1.0653.
//*********************************************************************************
static void test_swr_from_known_rho()
{
  Settings      s;
  makeSettings(s, DetectorType::AD8307);
  MeterReadings r = measure(s, CALFWD1_DEFAULT, CALREV2_DEFAULT);

  const double rho = 3.16227766 / 100.0;
  TEST_ASSERT_DOUBLE_WITHIN(0.001, (1 + rho) / (1 - rho), r.swr);
  TEST_ASSERT_TRUE(r.powerDetected);
}

//*********************************************************************************
//  Below MIN_PWR_FOR_SWR_CALC the reflection coefficient is noise, so SWR must
//  HOLD its previous value rather than swing.  Feeding both detectors their
//  lowest calibrated level puts the meter well under the threshold.
//*********************************************************************************
static void test_swr_holds_below_threshold()
{
  Settings s;
  makeSettings(s, DetectorType::AD8307);

  PowerMath     math;
  MeterReadings r;
  math.reset();

  //  No carrier: both channels at the bottom of the fit, so net power is ~0.
  math.addSample(CALFWD2_DEFAULT, CALREV2_DEFAULT, s, r);
  math.updateSwr(s, r);

  TEST_ASSERT_FALSE(r.powerDetected);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1.0, r.swr);    // untouched initial value
  TEST_ASSERT_FALSE(r.swrAlarm);
}

int main(int, char**)
{
  UNITY_BEGIN();
  RUN_TEST(test_ad8307_reads_cal_point_exactly);
  RUN_TEST(test_ad8307_coupler_selects_its_own_calibration);
  RUN_TEST(test_diode_coupler_selects_its_own_meter_cal);
  RUN_TEST(test_coupler_index_is_clamped);
  RUN_TEST(test_reverse_swaps_channels);
  RUN_TEST(test_reverse_only_calibration_leaves_forward_fit_alone);
  RUN_TEST(test_forward_calibration_moves_both_anchors);
  RUN_TEST(test_swr_from_known_rho);
  RUN_TEST(test_swr_holds_below_threshold);
  return UNITY_END();
}

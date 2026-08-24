//*********************************************************************************
//  test_uiformat.cpp  -  Host tests for the power readout format.
//
//  Two invariants worth pinning, both of which the panel depends on:
//
//    1. One decimal until the number needs four digits, so a reading crossing a
//       decade gains a digit and changes nothing else.  The old rule was three
//       significant figures and rendered 1.5 / 10.5 / 100.5 / 1500 W four
//       different ways.
//    2. Nothing wider than "999.9mW".  MeterScreen anchors its readouts at the
//       x where that string would still end at the right edge of the bar, so a
//       format that can emit something wider would run off the panel - and
//       nothing on the bench would necessarily show it.
//
//  UiFormat.cpp is INCLUDED rather than linked because lib/Ui is in the native
//  environment's lib_ignore: the rest of that directory needs LVGL, which does
//  not build on the host.  UiFormat itself has no such dependency - that is the
//  whole point of it being its own file - so one translation unit is enough.
//
//      pio test -e native
//*********************************************************************************
#include <unity.h>

#include "AppConfig.h"
#include "../../lib/Ui/UiFormat.cpp"

static char buf[24];

//  The transitions the format exists for.
static void test_one_decimal_across_decades()
{
  UiFormat::powerMw(buf, sizeof(buf), 1500.0);        // 1.5 W
  TEST_ASSERT_EQUAL_STRING("1.5W", buf);

  UiFormat::powerMw(buf, sizeof(buf), 10500.0);       // 10.5 W
  TEST_ASSERT_EQUAL_STRING("10.5W", buf);

  UiFormat::powerMw(buf, sizeof(buf), 100500.0);      // 100.5 W
  TEST_ASSERT_EQUAL_STRING("100.5W", buf);
}

//  Above 1 kW the decimal goes and the unit does NOT: this meter has no kW.
static void test_no_kilowatt_range()
{
  UiFormat::powerMw(buf, sizeof(buf), 1500000.0);     // 1500 W
  TEST_ASSERT_EQUAL_STRING("1500W", buf);

  UiFormat::powerMw(buf, sizeof(buf), 2000000.0);     // a 2 kW ceiling
  TEST_ASSERT_EQUAL_STRING("2000W", buf);

  UiFormat::powerMw(buf, sizeof(buf), 9000000.0);     // the clamp in PowerMath
  TEST_ASSERT_EQUAL_STRING("9000W", buf);
}

//  Only two units, so below a watt it stays in mW all the way down.
static void test_only_mw_and_w()
{
  UiFormat::powerMw(buf, sizeof(buf), 500.0);
  TEST_ASSERT_EQUAL_STRING("500.0mW", buf);

  UiFormat::powerMw(buf, sizeof(buf), 0.5);
  TEST_ASSERT_EQUAL_STRING("0.5mW", buf);

  UiFormat::powerMw(buf, sizeof(buf), 0.0);
  TEST_ASSERT_EQUAL_STRING("0.0mW", buf);
}

//  The two rounding boundaries.  999.95 mW must promote to "1.0W" rather than
//  print "1000.0mW", and 999.95 W must drop its decimal rather than "1000.0W".
static void test_rounding_boundaries()
{
  UiFormat::powerMw(buf, sizeof(buf), 999.9);
  TEST_ASSERT_EQUAL_STRING("999.9mW", buf);

  UiFormat::powerMw(buf, sizeof(buf), 999.95);
  TEST_ASSERT_EQUAL_STRING("1.0W", buf);

  UiFormat::powerMw(buf, sizeof(buf), 999900.0);
  TEST_ASSERT_EQUAL_STRING("999.9W", buf);

  UiFormat::powerMw(buf, sizeof(buf), 999950.0);
  TEST_ASSERT_EQUAL_STRING("1000W", buf);
}

//  The contract MeterScreen's anchors are computed from.
static void test_nothing_wider_than_the_anchor_sample()
{
  const size_t widest = strlen("999.9mW");

  const double probes[] = { 0.0, 0.05, 0.5, 999.9, 999.95, 1000.0, 1500.0,
                            10500.0, 100500.0, 999950.0, 1500000.0,
                            2000000.0, 9000000.0 };

  for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
    UiFormat::powerMw(buf, sizeof(buf), probes[i]);
    TEST_ASSERT_TRUE_MESSAGE(strlen(buf) <= widest, buf);
  }
}

//*********************************************************************************
//  CALIBRATION POINT READOUT
//
//  The Calibrate and About screens both print stored points through this, and
//  the reason it has two forms is a correctness one, not a cosmetic one: since
//  forward and reverse carry their own reference level, a screen showing a
//  single dBm figure after a reverse-only calibration would be describing one
//  fit and implying the other.  So the shared case stays exactly as it read
//  before, and the split case names both channels.
//*********************************************************************************
static char wide[48];

static void test_cal_point_shows_one_level_while_channels_agree()
{
  const CalPoint p = { 400, 400, 2.233f, 2.233f };
  UiFormat::calPoint(wide, sizeof(wide), "P1", p);
  TEST_ASSERT_EQUAL_STRING("P1: +40.0 dBm  F=2.233 R=2.233", wide);
}

static void test_cal_point_names_both_levels_once_they_split()
{
  //  What a reverse-only calibration at 20 dBm leaves behind.
  const CalPoint p = { 400, 200, 2.233f, 1.800f };
  UiFormat::calPoint(wide, sizeof(wide), "P1", p);
  TEST_ASSERT_EQUAL_STRING("P1: F+40.0 R+20.0  F=2.233 R=1.800", wide);
}

//  Neither form may be truncated by the buffer the screens hand it, and the
//  negative levels are what make the split form widest.
static void test_cal_point_fits_its_documented_buffer()
{
  const CalPoint probes[] = {
    { 400,  400,  2.233f, 2.233f },
    { 400,  200,  2.233f, 1.800f },
    { -100, -100, 0.100f, 0.100f },
    { -100, 530,  0.100f, 3.999f },
    { 530,  -100, 3.999f, 0.100f },
  };

  for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
    UiFormat::calPoint(wide, sizeof(wide), "P2", probes[i]);
    TEST_ASSERT_TRUE_MESSAGE(strlen(wide) < 40, wide);
  }
}

//*********************************************************************************
//  AUTOSCALE
//
//  The 11/22/55 preset ladder and its decade switch.  AutoScale only reads
//  the active coupler's detector and Settings::scaleRange, so the helper sets
//  just those.
//*********************************************************************************
static void makeScaleSettings(Settings& s)
{
  s.cal[0].detector = DetectorType::AD8307;
  s.scaleRange[0]   = SCALE_RANGE1;
  s.scaleRange[1]   = SCALE_RANGE2;
  s.scaleRange[2]   = SCALE_RANGE3;
  s.coupler         = 1;
}

//  A single push lands on the smallest preset that contains it.  The 30-sample
//  window is otherwise zero, so the max of the window IS this one reading.
static void test_autoscale_three_presets()
{
  Settings s;
  makeScaleSettings(s);

  {
    UiFormat::AutoScale a;
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 1.1, a.push(1.0, s));   // 1 mW  -> 1.1 mW
  }
  {
    UiFormat::AutoScale a;
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 2.2, a.push(2.0, s));   // 2 mW  -> 2.2 mW
  }
  {
    UiFormat::AutoScale a;
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 5.5, a.push(3.0, s));   // 3 mW  -> 5.5 mW
  }
  {
    UiFormat::AutoScale a;
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 55.0, a.push(30.0, s)); // 30 mW -> 55 mW
  }
}

//  Crossing a preset boundary re-scales; crossing the top preset (55 x decade)
//  steps up a full decade to the next 11.
static void test_autoscale_decade_boundary()
{
  Settings s;
  makeScaleSettings(s);

  UiFormat::AutoScale a;
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 5.5, a.push(5.5, s));   // exactly at 55 x 0.1
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 11.0, a.push(5.6, s));  // past it -> next decade's R1
}

//  The diode front end has no sub-mW range: its decade starts at 10000, so even
//  a small reading selects the 110 mW floor rather than a micro range.
static void test_autoscale_diode_decade_floor()
{
  Settings s;
  makeScaleSettings(s);
  s.cal[0].detector = DetectorType::Diode;

  UiFormat::AutoScale a;
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 110.0, a.push(10.0, s));
}

int main(int, char**)
{
  UNITY_BEGIN();
  RUN_TEST(test_one_decimal_across_decades);
  RUN_TEST(test_no_kilowatt_range);
  RUN_TEST(test_only_mw_and_w);
  RUN_TEST(test_rounding_boundaries);
  RUN_TEST(test_nothing_wider_than_the_anchor_sample);
  RUN_TEST(test_cal_point_shows_one_level_while_channels_agree);
  RUN_TEST(test_cal_point_names_both_levels_once_they_split);
  RUN_TEST(test_cal_point_fits_its_documented_buffer);
  RUN_TEST(test_autoscale_three_presets);
  RUN_TEST(test_autoscale_decade_boundary);
  RUN_TEST(test_autoscale_diode_decade_floor);
  return UNITY_END();
}

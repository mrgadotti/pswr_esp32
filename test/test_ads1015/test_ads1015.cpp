//*********************************************************************************
//  test_ads1015.cpp  -  Host tests for the ADS1015 driver, against a SIMULATED
//  chip rather than a bench.
//
//  Ads1015Detector.cpp is normally out of reach on the host: it is Arduino/Wire
//  hardware code, and env:native's lib_ignore excludes RfDetectors for exactly
//  that reason (see platformio.ini).  fakes/Arduino.h and fakes/Wire.h stand in
//  for the two headers it #includes, with a TwoWire that models just enough of
//  an ADS1015's register protocol (see fakes/Wire.h) to drive the REAL,
//  unmodified driver end to end - so this pins the one piece of the chain a
//  bench test cannot exercise safely: the raw register math (BIT_SHIFT, the
//  negative-code clamp, AIN0/AIN1 routing) and an absent device mid-read.
//
//  Included directly, the same way test_uiformat.cpp pulls in UiFormat.cpp:
//  the .cpp, not the library, so nothing here depends on the LDF resolving
//  RfDetectors (which it deliberately cannot, on native).
//
//      pio test -e native
//*********************************************************************************
#include <unity.h>

#include "../../lib/RfDetectors/Ads1015Detector.cpp"

//  4.096 V / 2048 counts, restated from Ads1015Detector.cpp's own LSB_VOLTS so
//  a test failure prints against the same constant the driver uses.
static constexpr float VOLTS_PER_COUNT = 0.002f;

//  A chip's conversion register is 12 bits, left-aligned in 16 - i.e. exactly
//  what the driver's `(int16_t)readRegister(...) >> BIT_SHIFT` expects to
//  shift back down.  Test data is written in raw 12-bit counts and converted
//  here, so a test reads as "the ADC saw this code" rather than "shifted left
//  by a magic 4".
static int16_t rawCode(int16_t counts12) { return (int16_t)(counts12 << 4); }

void setUp() { Wire = TwoWire{}; }   // fresh, all-absent bus before every test
void tearDown() {}

//*********************************************************************************
//  AIN0 and AIN1 must not cross - that is the one mistake here PowerMath could
//  never catch, since a fwd/rev swap in the driver looks identical to a coupler
//  wired backwards further up the chain.
//*********************************************************************************
static void test_reads_forward_and_reflected_independently()
{
  Wire.chips[0x48].present = true;
  Wire.chips[0x48].conv[0] = rawCode(1000);   // AIN0 (forward)  -> 2.000 V
  Wire.chips[0x48].conv[1] = rawCode(500);    // AIN1 (reflected) -> 1.000 V

  Ads1015Detector det(0, 0, 0x48);
  TEST_ASSERT_TRUE(det.begin());
  TEST_ASSERT_TRUE(det.present());

  float fwd, rev;
  det.read(fwd, rev);
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 1000 * VOLTS_PER_COUNT, fwd);
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 500  * VOLTS_PER_COUNT, rev);
}

//*********************************************************************************
//  "The clamp at zero is deliberate: these detectors are single-ended positive,
//  so a negative code is offset noise, not a negative signal" - README §1. A
//  negative conversion register must come back as exactly 0 V, not a small
//  negative voltage that then poisons downstream log math.
//*********************************************************************************
static void test_negative_code_clamps_to_zero_volts()
{
  Wire.chips[0x48].present = true;
  Wire.chips[0x48].conv[0] = rawCode(-50);    // offset noise on forward
  Wire.chips[0x48].conv[1] = rawCode(750);    // a real reading on reflected

  Ads1015Detector det(0, 0, 0x48);
  TEST_ASSERT_TRUE(det.begin());

  float fwd, rev;
  det.read(fwd, rev);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, fwd);
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 750 * VOLTS_PER_COUNT, rev);
}

//*********************************************************************************
//  No ACK on the bus: begin() must report absent rather than hang or fabricate
//  a reading, and a read() against that address must come back a quiet 0 V
//  rather than garbage - the situation a coupler unplugged mid-transmission
//  leaves behind.
//*********************************************************************************
static void test_absent_device_is_reported_and_reads_quietly()
{
  Ads1015Detector det(0, 0, 0x49);   // 0x49 never marked present on this bus
  TEST_ASSERT_FALSE(det.begin());
  TEST_ASSERT_FALSE(det.present());

  float fwd, rev;
  det.read(fwd, rev);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, fwd);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, rev);
}

//*********************************************************************************
//  probeAll() drives the multi-coupler numbering (README "MULTIPLE DEVICES"):
//  couplers are assigned in ADDRESS order, not probe order, so a bus with only
//  0x49 and 0x4B fitted must still come back as two detectors in that order,
//  each correctly identifying itself via diagId() for the debug screen.
//*********************************************************************************
static void test_probe_all_finds_populated_addresses_in_order()
{
  Wire.chips[0x49].present = true;
  Wire.chips[0x49].conv[0] = rawCode(100);
  Wire.chips[0x4B].present = true;
  Wire.chips[0x4B].conv[0] = rawCode(200);
  // 0x48 and 0x4A left absent.

  RfDetector* fitted[Ads1015Detector::ADDR_N] = { nullptr, nullptr, nullptr, nullptr };
  const uint8_t count = Ads1015Detector::probeAll(0, 0, 400000, fitted);

  TEST_ASSERT_EQUAL_UINT8(2, count);
  TEST_ASSERT_EQUAL_UINT8(0x49, fitted[0]->diagId());
  TEST_ASSERT_EQUAL_UINT8(0x4B, fitted[1]->diagId());

  float fwd, rev;
  fitted[0]->read(fwd, rev);
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 100 * VOLTS_PER_COUNT, fwd);
}

int main(int, char**)
{
  UNITY_BEGIN();
  RUN_TEST(test_reads_forward_and_reflected_independently);
  RUN_TEST(test_negative_code_clamps_to_zero_volts);
  RUN_TEST(test_absent_device_is_reported_and_reads_quietly);
  RUN_TEST(test_probe_all_finds_populated_addresses_in_order);
  return UNITY_END();
}

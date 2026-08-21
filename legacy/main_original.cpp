//*********************************************************************************
//**
//**  REFERENCE ONLY - NOT COMPILED.  This is the original monolithic firmware,
//**  kept for reference and backup while the project was split into lib/ modules.
//**  It is NOT part of any PlatformIO environment: nothing #includes it and no
//**  build rule touches this directory.  Do not "fix" or reformat it - divergence
//**  from what shipped is what makes it useless as a reference.
//**
//**
//** PSWR_CYD.ino  -  RF Power & SWR Meter for the ESP32 Cheap Yellow Display
//**
//** Single-file Arduino-IDE port of the menu driven Multi Display RF Power and
//** SWR Meter by Loftur E. Jonasson (TF3LJ/VE2AO), RA8875/Teensy variant with
//** later additions by J.G. Holstein.
//**
//** Original target..: Teensy 4.x + RA8875 800x480 + GSL1680 + AD7991
//** This port (2026)..: ESP32-2432S028R "Cheap Yellow Display" (CYD)
//**                     - ILI9341 320x240 via TFT_eSPI  (pins in User_Setup.h)
//**                     - XPT2046 resistive touch on its own VSPI bus
//**                     - ADS1015 (I2C) replaces the AD7991; AIN0=Fwd, AIN1=Rev
//**                     - dual core: sampler on core 0, UI/math on core 1
//**
//** The original screens are designed for an 800x480 panel. They are reproduced
//** here as the SAME display modes and the SAME menu structure, re-laid-out for
//** the 320x240 CYD. Hardware-specific subsystems of the RA8875 build that do
//** not exist on a stand-alone CYD are intentionally omitted: WiFi / second
//** ESP32 firmware upload, the frequency counter, the QWERTY SSID keyboard and
//** USB serial passthrough.
//**
//** If no ADS1015 is detected at boot, the meter runs on a FIXED mock of
//** 80 W forward and SWR 1.5 so the whole interface can be exercised.
//**
//** GPL v3, as the original.  See <http://www.gnu.org/licenses/>.
//**
//** Libraries (Library Manager):
//**   - TFT_eSPI (Bodmer)  with the CYD User_Setup.h installed
//**   - XPT2046_Touchscreen (Paul Stoffregen)
//**   (ADS1015 is driven directly over Wire, no extra library required)
//**
//*********************************************************************************

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <math.h>

//*********************************************************************************
//  COLOUR ALIASES (map the original RA8875 colour names onto TFT_eSPI)
//*********************************************************************************
#define C_BLACK    TFT_BLACK
#define C_WHITE    TFT_WHITE
#define C_GREY     0x8410
#define C_DKGREY   0x4208
#define C_GREEN    TFT_GREEN
#define C_RED      TFT_RED
#define C_BLUE     TFT_BLUE
#define C_YELLOW   TFT_YELLOW
#define C_ORANGE   TFT_ORANGE
#define C_CYAN     TFT_CYAN
#define C_PINK     TFT_PINK
#define C_LTBLUE   0x06FF
#define C_NAVY     TFT_NAVY
#define C_BTNBG    0x20C3      // original inactive button background
#define C_BTNACT   0x12A0      // original active button background
#define C_MENUHI   0xB6DF      // ESP32-style menu selection bar (light blue)

//*********************************************************************************
//  DISPLAY / TOUCH HARDWARE  (TFT pins come from the TFT_eSPI User_Setup.h)
//*********************************************************************************
TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W   320
#define SCREEN_H   240

// XPT2046 on dedicated VSPI bus (CYD wiring)
#define TOUCH_CLK   25
#define TOUCH_MISO  39
#define TOUCH_MOSI  32
#define TOUCH_CS    33
#define TOUCH_IRQ   36
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

#define TOUCH_MIN_X        200
#define TOUCH_MAX_X        3700
#define TOUCH_MIN_Y        240
#define TOUCH_MAX_Y        3800
#define TOUCH_MIN_PRESSURE 300

// CYD onboard RGB LED (active LOW)
#define LED_R   4     // SWR alarm
#define LED_G   16    // power detected
#define LED_B   17    // unused

// I2C for the ADS1015 (CYD CN1 free pins)
#define I2C_SDA       27
#define I2C_SCL       22
#define ADS1015_ADDR  0x48

//*********************************************************************************
//  METER CONFIGURATION  (ported from PSWR_T.h)
//*********************************************************************************
#define VERSION   "7.03-CYD"

enum DetectorType : uint8_t { DET_AD8307 = 0, DET_DIODE = 1 };
#define DETECTOR_DEFAULT   DET_AD8307

// AD8307 two-point calibration defaults (30:1 Tandem Match)
#define CAL1_NOR_VALUE     400      // 40.0 dBm
#define CAL2_NOR_VALUE     100      // 10.0 dBm
#define CALFWD1_DEFAULT    2.233
#define CALREV1_DEFAULT    2.233
#define CALFWD2_DEFAULT    1.528
#define CALREV2_DEFAULT    1.528
#define LOGAMP_SLOPE       23.5     // mV/dB nominal (One-Level cal)
#define CAL_INP_QUALITY    12.0     // dB min Fwd/Rev separation to accept cal
#define CAL_DBM_MAX        530
#define CAL_DBM_MIN       -100

// Diode / Bruene bridge defaults
#define BRIDGE_COUPLING    24.0     // transformer turns ratio (24:1)
#define METER_CAL          1.00
#define D_VDROP            0.25

// Power / SWR thresholds (mW)
#define MIN_PWR_FOR_SWR_CALC_AD8307   20.0
#define MIN_PWR_FOR_SWR_CALC_DIODE    30.0
#define SWR_ALARM_DEFAULT        30   // 3.0:1 (40 disables)
#define SWR_THRESHOLD_DEFAULT    10   // mW

// Autoscale ranges, up to 3 per decade (... 11 22 55 110 220 550 ...)
#define SCALE_RANGE1   11
#define SCALE_RANGE2   22
#define SCALE_RANGE3   55

// Sampling / buffers (ADS1015 over I2C ~ 500 pairs/s)
#define SAMPLE_MS        2
#define BUF_SHORT        50          // 100 ms
#define AVG_BUF1S        500
#define AVG_BUFSHORT     50
#define AVG_BUFSWR       10
#define POLL_MS          10

// PEP envelope options selectable from the front panel / menu, in BUF_SHORT units
// 100 ms, 1 s, 2.5 s windows
const uint16_t PEP_OPTIONS[3] = { 1, 10, 25 };
const char    *PEP_LABELS[3]  = { "100ms", "1s", "2.5s" };

#ifndef SQR
#define SQR(x) ((x)*(x))
#endif

//*********************************************************************************
//  GLOBAL STATE
//*********************************************************************************
Preferences prefs;
bool adcPresent = false;

// circular buffer fed by the sampler task on core 0
typedef struct {
  volatile float   fwd[256];
  volatile float   rev[256];
  volatile uint8_t writeIdx;
  uint8_t          readIdx;
} adbuffer_t;
adbuffer_t samplerBuf;
portMUX_TYPE samplerMutex = portMUX_INITIALIZER_UNLOCKED;

// persisted settings
typedef struct { int16_t db10m; float Fwd; float Rev; } cal_t;

struct {
  cal_t    cal_AD[2];
  uint8_t  detector;
  uint8_t  SWR_alarm_trig;          // SWR*10, 40 = off
  uint16_t SWR_alarm_pwr_thresh;    // mW
  uint8_t  ScaleRange[3];
  uint8_t  mode_display;            // POWER_BARPEP / POWER_MIXED / POWER_CLEAN_DBM / MODULATIONSCOPE
  uint8_t  pep_idx;                 // index into PEP_OPTIONS / PEP_LABELS
  uint8_t  coupler;                 // 1 or 2 (display/label only on CYD)
  uint8_t  band;                    // 0=HF 1=6M 2=2M 3=70cm (label only)
  float    meter_cal;               // diode one-point cal factor
} R;

// display modes (same numbering as the original)
enum : uint8_t { POWER_BARPEP = 1, POWER_MIXED = 2, POWER_CLEAN_DBM = 3,
                 MODULATIONSCOPE = 4, MAX_MODE = 4 };
const char *bandNames[4] = { "HF", "6M", "2M", "70cm" };

// measurement results
float  v_fwd, v_rev;
bool   Reverse = false;
double f_inst = 0, r_inst = 0;
double fwdPowerMw = 0, refPowerMw = 0;
double netPowerMw = 0,  netPowerDb = -90;
double peakPowerDb = -90,  peakPowerMw = 0;
double pepPowerDb = -90, pepPowerMw = 0;
double avgPowerMw = 0,   avg1sPowerMw = 0;
double ad8307FwdDbm = -90,  ad8307RevDbm = -90;
double swr = 1.0, swrAvg = 1.0;
bool   swrAlarm = false;
bool   powerDetected = false;

char fmtBuf[24];

//*********************************************************************************
//  ADS1015  (raw I2C, single-shot, PGA +/-4.096 V, 2400 SPS)
//*********************************************************************************
#define ADS_REG_CONV    0x00
#define ADS_REG_CONFIG  0x01
#define ADS_CFG_BASE    (0x8000 | 0x0200 | 0x0100 | 0x00C0 | 0x0003)
#define ADS_MUX_AIN0    0x4000
#define ADS_MUX_AIN1    0x5000
#define ADS_LSB_VOLTS   0.002f

static bool adsWriteConfig(uint16_t cfg)
{
  Wire.beginTransmission(ADS1015_ADDR);
  Wire.write(ADS_REG_CONFIG);
  Wire.write((uint8_t)(cfg >> 8));
  Wire.write((uint8_t)(cfg & 0xFF));
  return (Wire.endTransmission() == 0);
}
static int16_t adsReadConversion(void)
{
  Wire.beginTransmission(ADS1015_ADDR);
  Wire.write(ADS_REG_CONV);
  if (Wire.endTransmission() != 0) return 0;
  Wire.requestFrom((uint8_t)ADS1015_ADDR, (uint8_t)2);
  if (Wire.available() < 2) return 0;
  int16_t raw = (Wire.read() << 8) | Wire.read();
  return raw >> 4;
}
static float adsReadChannel(uint16_t mux)
{
  adsWriteConfig(ADS_CFG_BASE | mux);
  delayMicroseconds(600);
  int16_t raw = adsReadConversion();
  if (raw < 0) raw = 0;
  return raw * ADS_LSB_VOLTS;
}
static bool adsDetect(void)
{
  Wire.beginTransmission(ADS1015_ADDR);
  return (Wire.endTransmission() == 0);
}

//*********************************************************************************
//  FIXED MOCK  -  no ADS1015: synthesise detector voltages that the normal math
//  turns into exactly 80 W forward and SWR 1.5, for the active detector type.
//  (Derived from current calibration so it stays 80 W even after recalibration.)
//*********************************************************************************
#define MOCK_WATTS   129.5
#define MOCK_SWR     1.5

static void mockVoltages(float *fv, float *rv)
{
  double rho      = (MOCK_SWR - 1.0) / (MOCK_SWR + 1.0);     // 0.2
  double fwd_mw   = MOCK_WATTS * 1000.0;                     // 80000 mW
  double ref_mw   = fwd_mw * rho * rho;                      // 3200 mW

  if (R.detector == DET_AD8307)
  {
    double fdbm = 10.0 * log10(fwd_mw);                      // ~49.03 dBm
    double rdbm = 10.0 * log10(ref_mw);                      // ~35.05 dBm
    double db0  = R.cal_AD[0].db10m / 10.0;
    double ddb  = (R.cal_AD[1].db10m - R.cal_AD[0].db10m) / 10.0;
    double slopeF = (R.cal_AD[1].Fwd - R.cal_AD[0].Fwd) / ddb;   // V per dB
    double slopeR = (R.cal_AD[1].Rev - R.cal_AD[0].Rev) / ddb;
    *fv = R.cal_AD[0].Fwd + (fdbm - db0) * slopeF;
    *rv = R.cal_AD[0].Rev + (rdbm - db0) * slopeR;
  }
  else
  {
    double vrms_f = sqrt(fwd_mw * 50.0 / 1000.0);            // line Vrms
    double vrms_r = vrms_f * rho;
    double cf = vrms_f / (BRIDGE_COUPLING * R.meter_cal);    // at the detector
    double cr = vrms_r / (BRIDGE_COUPLING * R.meter_cal);
    *fv = (cf - D_VDROP) * 1.41421356 + D_VDROP;             // invert diode eq.
    *rv = (cr - D_VDROP) * 1.41421356 + D_VDROP;
    if (*fv < 0) *fv = 0;
    if (*rv < 0) *rv = 0;
  }
}

//*********************************************************************************
//  SAMPLER TASK  -  core 0
//*********************************************************************************
void samplerTask(void *param)
{
  TickType_t lastWake = xTaskGetTickCount();
  for (;;)
  {
    float fv, rv;
    if (adcPresent) { fv = adsReadChannel(ADS_MUX_AIN0); rv = adsReadChannel(ADS_MUX_AIN1); }
    else           { mockVoltages(&fv, &rv); }

    portENTER_CRITICAL(&samplerMutex);
    uint8_t in = samplerBuf.writeIdx;
    samplerBuf.fwd[in] = fv;
    samplerBuf.rev[in] = rv;
    samplerBuf.writeIdx = in + 1;
    portEXIT_CRITICAL(&samplerMutex);

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SAMPLE_MS));
  }
}

//*********************************************************************************
//  MEASUREMENT MATH  (ported from PSWRsamplerBuf.ino)
//*********************************************************************************
static void pswr_determine_dBm(void)
{
  double delta_db  = (double)(R.cal_AD[1].db10m - R.cal_AD[0].db10m) / 10.0;
  double delta_Fdb = delta_db / (R.cal_AD[1].Fwd - R.cal_AD[0].Fwd);
  double delta_Rdb = delta_db / (R.cal_AD[1].Rev - R.cal_AD[0].Rev);

  ad8307FwdDbm = (v_fwd - R.cal_AD[0].Fwd) * delta_Fdb + R.cal_AD[0].db10m / 10.0;
  ad8307RevDbm = (v_rev - R.cal_AD[0].Rev) * delta_Rdb + R.cal_AD[0].db10m / 10.0;

  if (ad8307FwdDbm >= ad8307RevDbm) Reverse = false;
  else { double t = ad8307RevDbm; ad8307RevDbm = ad8307FwdDbm; ad8307FwdDbm = t; Reverse = true; }
}

static uint16_t pep_window(void) { return PEP_OPTIONS[R.pep_idx]; }

static void determine_power_pep_pk(void)
{
  static int32_t  buff_short[BUF_SHORT];
  static int32_t  pepbuff[25];                  // max PEP window
  static double   p_avg_buf[AVG_BUFSHORT];
  static double   p_avg_buf1s[AVG_BUF1S];
  static double   p_plus = 0, p_1splus = 0;
  static uint16_t a = 0, b = 0, d = 0, e = 0;
  int32_t pk;

  if (R.detector == DET_AD8307)
  {
    pswr_determine_dBm();
    f_inst = pow(10, ad8307FwdDbm / 20.0);
    fwdPowerMw = SQR(f_inst);
    r_inst = pow(10, ad8307RevDbm / 20.0);
    refPowerMw = SQR(r_inst);
  }
  else
  {
    float fv = v_fwd, rv = v_rev;
    if (fv >= rv) Reverse = false;
    else { float t = rv; rv = fv; fv = t; Reverse = true; }

    f_inst = fv;
    if (f_inst >= D_VDROP) f_inst = (f_inst - D_VDROP) / 1.41421356 + D_VDROP;
    f_inst = f_inst * BRIDGE_COUPLING * R.meter_cal;
    fwdPowerMw = 1000.0 * SQR(f_inst) / 50.0;

    r_inst = rv;
    if (r_inst >= D_VDROP) r_inst = (r_inst - D_VDROP) / 1.41421356 + D_VDROP;
    r_inst = r_inst * BRIDGE_COUPLING * R.meter_cal;
    refPowerMw = 1000.0 * SQR(r_inst) / 50.0;
  }

  if (fwdPowerMw > 9000000) fwdPowerMw = 9000000;
  if (refPowerMw > 9000000) refPowerMw = 9000000;

  netPowerMw = fwdPowerMw - refPowerMw;
  if (netPowerMw < 0) netPowerMw = -netPowerMw;
  netPowerDb = (netPowerMw > 1e-9) ? 10.0 * log10(netPowerMw) : -90.0;

  // PK 100 ms
  buff_short[a++] = (int32_t)(100 * netPowerDb);
  if (a == BUF_SHORT) a = 0;
  pk = -0x7fffffff;
  for (uint16_t x = 0; x < BUF_SHORT; x++) if (pk < buff_short[x]) pk = buff_short[x];
  peakPowerDb = pk / 100.0;

  // PEP window (selectable)
  if (a == 0)
  {
    uint16_t win = pep_window();
    pepbuff[b++] = (int32_t)(100 * peakPowerDb);
    if (b >= win) b = 0;
    pk = -0x7fffffff;
    for (uint16_t x = 0; x < win; x++) if (pk < pepbuff[x]) pk = pepbuff[x];
    pepPowerDb = pk / 100.0;
  }
  if (pepPowerDb < peakPowerDb) pepPowerDb = peakPowerDb;

  // AVG 100 ms
  p_avg_buf[d++] = netPowerMw;
  p_plus += netPowerMw;
  if (d == AVG_BUFSHORT) d = 0;
  p_plus -= p_avg_buf[d];
  avgPowerMw = p_plus / (AVG_BUFSHORT - 1);

  // AVG 1 s
  p_avg_buf1s[e++] = netPowerMw;
  p_1splus += netPowerMw;
  if (e == AVG_BUF1S) e = 0;
  p_1splus -= p_avg_buf1s[e];
  avg1sPowerMw = p_1splus / (AVG_BUF1S - 1);
}

static void pswr_sync_from_sampler(void)
{
  for (;;)
  {
    portENTER_CRITICAL(&samplerMutex);
    uint8_t in = samplerBuf.writeIdx;
    portEXIT_CRITICAL(&samplerMutex);
    if (samplerBuf.readIdx == in) break;

    uint8_t out = samplerBuf.readIdx;
    v_fwd = samplerBuf.fwd[out];
    v_rev = samplerBuf.rev[out];
    samplerBuf.readIdx = out + 1;
    determine_power_pep_pk();
  }
}

static void calc_SWR_and_power(void)
{
  static double   swrAvgBuf[AVG_BUFSWR];
  static double   swrAccum = 0;
  static uint16_t a = 0;

  double min_calc = (R.detector == DET_AD8307) ? MIN_PWR_FOR_SWR_CALC_AD8307
                                               : MIN_PWR_FOR_SWR_CALC_DIODE;
  peakPowerMw  = pow(10, peakPowerDb  / 10.0);
  pepPowerMw = pow(10, pepPowerDb / 10.0);

  if (netPowerMw > min_calc)
  {
    double rho = r_inst / f_inst;
    if (rho > 0.999) rho = 0.999;
    swr = (1 + rho) / (1 - rho);
    if ((R.SWR_alarm_trig != 40) && ((swr * 10) >= R.SWR_alarm_trig) &&
        (netPowerMw > R.SWR_alarm_pwr_thresh))
      swrAlarm = true;
  }
  powerDetected = (netPowerMw > min_calc);

  swrAvgBuf[a++] = swr;
  swrAccum += swr;
  if (a == AVG_BUFSWR) a = 0;
  swrAccum -= swrAvgBuf[a];
  swrAvg = swrAccum / (AVG_BUFSWR - 1);
}

//*********************************************************************************
//  PRINT / FORMAT HELPERS  (ported from PSWRprintFunc.ino)
//*********************************************************************************
static void print_swr(double s)
{
  uint16_t sub = (uint16_t)(s * 100);
  uint16_t sup = sub / 100; sub = sub % 100;
  if (s < 2.0)        sprintf(fmtBuf, "%u.%02u", sup, sub);
  else if (s <= 10.0) sprintf(fmtBuf, "%u.%01u", sup, sub / 10);
  else if (s <= 1000) sprintf(fmtBuf, "%4u", (uint16_t)s);
  else                sprintf(fmtBuf, "9999");
}
static void print_dbm(int16_t db10m)
{
  int16_t t = (db10m < 0) ? -db10m : db10m;
  int16_t w = t / 10; t = t % 10;
  if (db10m < 0) sprintf(fmtBuf, "-%u.%udBm", w, t);
  else           sprintf(fmtBuf, "%u.%udBm", w, t);
}
static void print_p_mw(double pwr)
{
  int8_t r = 3;
  const char *ind[] = { "pW","nW","uW","mW","W","kW","MW","GW" };
  double p = (pwr < 0) ? -pwr : pwr;
  if (R.detector == DET_AD8307)
  {
    if (pwr < 0.001) { sprintf(fmtBuf, "0 uW"); return; }
    while ((p < 1.0) && (r > 0)) { p *= 1000.0; r--; }
  }
  while ((p >= 1000) && (r < 7)) { p /= 1000.0; r++; }
  if      (p >= 99.95) sprintf(fmtBuf, "%u%s",   (uint16_t)p, ind[r]);
  else if (p >= 9.995) sprintf(fmtBuf, "%.1f%s", p, ind[r]);
  else                 sprintf(fmtBuf, "%.2f%s", p, ind[r]);
}

//*********************************************************************************
//  AUTOSCALE  (ported from PSWRdisplay.ino scale_ranges)
//*********************************************************************************
#define SCALE_BUFFER  30
static double scale_ranges(double p_mw)
{
  static double   peak_buff[SCALE_BUFFER];
  static uint16_t a = 0;
  double max, scale;
  double decade = (R.detector == DET_AD8307) ? 1.0 : 10000.0;

  peak_buff[a++] = p_mw * 1000.0;     // uW
  if (a == SCALE_BUFFER) a = 0;
  max = 0;
  for (uint16_t b = 0; b < SCALE_BUFFER; b++) if (max < peak_buff[b]) max = peak_buff[b];

  while ((decade * R.ScaleRange[2]) < max) decade *= 10;
  if      (max >= decade * R.ScaleRange[1]) scale = decade * R.ScaleRange[2];
  else if (max >= decade * R.ScaleRange[0]) scale = decade * R.ScaleRange[1];
  else                                      scale = decade * R.ScaleRange[0];
  return scale / 1000.0;              // mW
}
static void scalePowerMeter(double fs, double *out_fs, char *range)
{
  int8_t r = 3;
  const char *ind[] = { "pW","nW","uW","mW","W","kW","MW","GW" };
  while (fs < 1.0 && r > 0)   { fs *= 1000; r--; }
  while (fs >= 1000 && r < 7) { fs /= 1000; r++; }
  *out_fs = fs;
  strcpy(range, ind[r]);
}

//*********************************************************************************
//  WIDGETS  -  PowerMeter / VSWRmeter / ModulationScope, faithful to PSWRtft.cpp
//  (addressed by index, sprite-rendered; ticks + numbers BELOW each bar)
//*********************************************************************************
TFT_eSprite spr = TFT_eSprite(&tft);

// ---- up to two power bars ----
int16_t pmX[2], pmY[2], pmLen[2], pmH[2];
double  pmScaleVal[2] = { -1, -1 };
char    pmRangeStr[2][6];
bool    pmFrame[2]    = { false, false };

static void pmInit(uint8_t m, int16_t x, int16_t y, int16_t len, int16_t h)
{ pmX[m]=x; pmY[m]=y; pmLen[m]=len; pmH[m]=h; pmScaleVal[m]=-1; pmRangeStr[m][0]=0; pmFrame[m]=false; }

static void pmScaleDraw(uint8_t m, double s, const char *range)
{
  if (pmFrame[m] && s == pmScaleVal[m] && strcmp(range, pmRangeStr[m]) == 0) return;
  pmScaleVal[m] = s; strcpy(pmRangeStr[m], range);
  int16_t x = pmX[m], y = pmY[m], len = pmLen[m], h = pmH[m];

  if (!pmFrame[m]) { tft.drawRect(x, y, len + 2, h + 2, C_WHITE); pmFrame[m] = true; }
  tft.fillRect(x - 4, y + h + 2, len + 26, 14, C_BLACK);

  double divisor = (((int)(s * 10 + 0.1)) % 11 == 0) ? 11 : 10;
  tft.setTextFont(1);
  tft.setTextColor(C_WHITE, C_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextPadding(0);

  tft.drawFastVLine(x + 1, y + h + 2, 5, C_WHITE);
  tft.drawString("0", x + 1, y + h + 7);

  char b[10];
  double j = s / divisor;
  for (double i = len / divisor; i <= len + 0.5; i += len / divisor, j += s / divisor)
  {
    int16_t xx = x + (int16_t)(i + 0.5);
    tft.drawFastVLine(xx, y + h + 2, 5, C_WHITE);
    if (i >= len - 1) tft.drawString(range, xx + 1, y + h + 7);
    else {
      double k; bool sub = (modf(j, &k) > 0.05) && (modf(j, &k) < 0.95);
      if (sub) sprintf(b, "%.1f", j); else sprintf(b, "%d", (int)(j + 0.5));
      tft.drawString(b, xx, y + h + 7);
    }
  }
  for (double i = len / (2 * divisor); i <= len; i += len / divisor)
    tft.drawFastVLine(x + (int16_t)(i + 0.5), y + h + 2, 3, C_WHITE);
  tft.setTextDatum(TL_DATUM);
  tft.fillRect(x + 1, y + 1, len, h, C_BLACK);
}

static void pmGraph(uint8_t m, double low, double high, double maxlevel)
{
  int16_t len = pmLen[m], h = pmH[m];
  if (low > maxlevel) low = maxlevel;
  if (high > maxlevel) high = maxlevel;
  if (low < 0) low = 0; if (high < 0) high = 0;
  int16_t lo = len * low / maxlevel;
  int16_t hi = len * high / maxlevel;
  if (hi < lo) hi = lo;

  spr.createSprite(len, h);
  spr.fillSprite(C_BLACK);
  if (lo > 0)     spr.fillRect(0, 0, lo, h, C_YELLOW);
  if (hi > lo)    spr.fillRect(lo, 0, hi - lo, h, C_GREEN);
  spr.pushSprite(pmX[m] + 1, pmY[m] + 1);
  spr.deleteSprite();
}

// ---- one SWR bar ----
int16_t swX, swY, swLen, swH;
bool    swFrame = false;
static void swrInit(int16_t x, int16_t y, int16_t len, int16_t h)
{ swX=x; swY=y; swLen=len; swH=h; swFrame=false; }

static int16_t swrPos(double v) { if (v<1) v=1; if (v>10) v=10; return (int16_t)(swLen*log10(v)); }

static void swrScaleDraw(void)
{
  if (swFrame) return; swFrame = true;
  const double mk[] = { 1,1.5,2,3,4,5,6,7,8,10 };
  const double tk[] = { 1.1,1.2,1.3,1.4,1.6,1.7,1.8,1.9,2.2,2.4,2.6,2.8,
                        3.2,3.4,3.6,3.8,4.5,5.5,6.5,9 };
  tft.drawRect(swX, swY, swLen + 2, swH + 2, C_WHITE);
  tft.fillRect(swX - 4, swY + swH + 2, swLen + 34, 14, C_BLACK);
  tft.setTextFont(1); tft.setTextColor(C_WHITE, C_BLACK);
  tft.setTextDatum(TC_DATUM); tft.setTextPadding(0);
  char b[8];
  for (uint8_t i = 0; i < 10; i++) {
    int16_t o = swX + 1 + swrPos(mk[i]);
    tft.drawFastVLine(o, swY + swH + 2, 5, C_WHITE);
    if (mk[i] < 10) {
      double a; bool sub = (modf(mk[i], &a) > 0.05) && (modf(mk[i], &a) < 0.95);
      if (sub) sprintf(b, "%.1f", mk[i]); else sprintf(b, "%d", (int)(mk[i] + 0.5));
      tft.drawString(b, o, swY + swH + 7);
    } else {
      tft.setTextDatum(TR_DATUM); tft.drawString("SWR", o + 1, swY + swH + 7);
      tft.setTextDatum(TC_DATUM);
    }
  }
  for (uint8_t i = 0; i < 20; i++)
    tft.drawFastVLine(swX + 1 + swrPos(tk[i]), swY + swH + 2, 3, C_WHITE);
  tft.setTextDatum(TL_DATUM);
}

static void swrGraph(double mid, double alarm, double value)
{
  int16_t w = swrPos(value), xm = swrPos(mid), xa = swrPos(alarm);
  spr.createSprite(swLen, swH);
  spr.fillSprite(C_BLACK);
  int16_t wg = (w < xm) ? w : xm;
  int16_t wo = (w < xa) ? w : xa;
  if (wg > 0)  spr.fillRect(0, 0, wg, swH, C_GREEN);
  if (wo > wg) spr.fillRect(wg, 0, wo - wg, swH, C_ORANGE);
  if (w  > wo) spr.fillRect(wo, 0, w - wo, swH, C_RED);
  spr.pushSprite(swX + 1, swY + 1);
  spr.deleteSprite();
}

// ---- modulation scope ----
#define SCOPE_BUF 304
int16_t scX, scY, scLen, scH, scY0, scMax;
int16_t scNew[SCOPE_BUF], scOld[SCOPE_BUF];
int16_t scIn = 0, scOut = 0;
bool    scDrawn = false;

static void scopeInit(int16_t x, int16_t y, int16_t len, int16_t h)
{
  scX=x; scY=y; scLen=(len>SCOPE_BUF?SCOPE_BUF:len); scH=h;
  scY0 = y + 1 + h/2; scMax = h/2; scIn=scOut=0; scDrawn=false;
  for (int16_t i=0;i<SCOPE_BUF;i++){ scNew[i]=0; scOld[i]=0; }
}
static void scopeAdd(double level, double fullscale)
{
  if (fullscale < level) fullscale = level;
  if (fullscale < 0.05)  fullscale = 0.05;
  scNew[scIn++] = (int16_t)(level / fullscale * scMax);
  if (scIn >= scLen) scIn = 0;
}
static void scopeUpdate(void)
{
  if (!scDrawn) { tft.drawRect(scX, scY, scLen + 2, scH + 2, C_WHITE); scDrawn = true; }
  while (scOut != scIn)
  {
    int16_t ys, ye, yl;
    if (scNew[scOut] > scOld[scOut]) {
      ys = scY0 - scNew[scOut]; ye = scY0 - scOld[scOut]; yl = ye - ys;
      if (yl>0) tft.drawFastVLine(scX+1+scOut, ys+1, yl, C_GREEN);
      tft.drawPixel(scX+1+scOut, ys, C_YELLOW);
      ys = scY0 + scOld[scOut]; ye = scY0 + scNew[scOut]; yl = ye - ys;
      if (yl>0) tft.drawFastVLine(scX+1+scOut, ys, yl, C_GREEN);
      tft.drawPixel(scX+1+scOut, ye, C_YELLOW);
    } else if (scNew[scOut] < scOld[scOut]) {
      ys = scY0 - scOld[scOut]; ye = scY0 - scNew[scOut]; yl = ye - ys;
      if (yl>0) tft.drawFastVLine(scX+1+scOut, ys, yl, C_BLACK);
      tft.drawPixel(scX+1+scOut, ye, C_YELLOW);
      ys = scY0 + scNew[scOut]; ye = scY0 + scOld[scOut]; yl = ye - ys;
      if (yl>0) tft.drawFastVLine(scX+1+scOut, ys+1, yl, C_BLACK);
      tft.drawPixel(scX+1+scOut, ys, C_YELLOW);
    } else if (scNew[scOut] == 0) tft.drawPixel(scX+1+scOut, scY0, C_YELLOW);
    scOld[scOut] = scNew[scOut];
    scOut++; if (scOut >= scLen) scOut = 0;
  }
}

//*********************************************************************************
//  UI STATE
//*********************************************************************************
enum : uint8_t { UI_INTRO = 0, UI_MAIN };
uint8_t  uiState = UI_INTRO;
uint32_t introEndTime = 0;
bool     uiNeedsRedraw = true;
double   scopeFullscale = 100000.0;        // modulation scope fullscale (mW)

#define MODE_INTRO_MS  1400

const char *modeName1[] = { "", "Peak Envelope", "Forward and", "dBm Meter", "Modulation" };
const char *modeName2[] = { "", "Power Bargraph", "Reflected Power", "and SWR Bargraph", "Scope" };

//*********************************************************************************
//  FRONT PANEL BUTTON ROW (always visible on the meter screens, like the original)
//*********************************************************************************
struct Btn { int x, y, w, h; uint16_t color; };
#define FP_N 5
Btn fp[FP_N];
#define FP_Y   178
#define FP_H   58

static void fpSetup(void)
{
  int gap = 4, bw = (SCREEN_W - 6 * gap) / 5;
  for (uint8_t i = 0; i < FP_N; i++) { fp[i].x = gap + i * (bw + gap); fp[i].y = FP_Y; fp[i].w = bw; fp[i].h = FP_H; fp[i].color = C_BTNBG; }
}
static void fpLabel(uint8_t i, char *out)
{
  switch (i) {
    case 0: sprintf(out, "CPL%u", R.coupler); break;
    case 1: strcpy(out, bandNames[R.band]); break;
    case 2: strcpy(out, PEP_LABELS[R.pep_idx]); break;
    case 3: strcpy(out, "MODE"); break;
    default: strcpy(out, "SET"); break;
  }
}
static void fpDraw(uint8_t i, uint16_t fill)
{
  char lbl[8]; fpLabel(i, lbl);
  tft.fillRoundRect(fp[i].x, fp[i].y, fp[i].w, fp[i].h, 6, fill);
  tft.drawRoundRect(fp[i].x, fp[i].y, fp[i].w, fp[i].h, 6, C_DKGREY);
  tft.setTextFont(2);
  tft.setTextColor(C_WHITE, fill);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(0);
  tft.drawString(lbl, fp[i].x + fp[i].w / 2, fp[i].y + fp[i].h / 2);
  tft.setTextDatum(TL_DATUM);
}
static void fpDrawAll(void) { for (uint8_t i = 0; i < FP_N; i++) fpDraw(i, fp[i].color); }
static bool fpInside(uint8_t i, int px, int py)
{ return px >= fp[i].x && px <= fp[i].x + fp[i].w && py >= fp[i].y && py <= fp[i].y + fp[i].h; }

//*********************************************************************************
//  MAIN SCREEN  -  geometry per display mode (mirrors the original layouts)
//*********************************************************************************
#define MX   9
#define MLEN 300

static void mainScreenInit(void)
{
  tft.fillScreen(C_BLACK);
  swFrame = false;
  scDrawn = false;

  switch (R.mode_display)
  {
    case POWER_MIXED:
      pmInit(0, MX,  2, MLEN, 16);     // Fwd
      pmInit(1, MX, 36, MLEN, 16);     // Ref
      swrInit(  MX, 70, MLEN, 14);     // SWR
      break;
    case MODULATIONSCOPE:
      scopeInit(MX, 2, MLEN, 150);
      break;
    default:                            // POWER_BARPEP and POWER_CLEAN_DBM
      pmInit(0, MX,  2, MLEN, 28);     // Power
      swrInit(  MX, 50, MLEN, 18);     // SWR
      break;
  }
  fpDrawAll();
}

static void mainDrawGraphs(void)
{
  double adj, swr_alm = (R.SWR_alarm_trig != 40) ? R.SWR_alarm_trig / 10.0 : 10.0;
  double swr_mid = 2.0; if (swr_alm < swr_mid) swr_mid = swr_alm;
  char range[6];

  if (R.mode_display == MODULATIONSCOPE) { scopeUpdate(); return; }

  if (R.mode_display == POWER_MIXED)
  {
    double scale = scale_ranges(fwdPowerMw);
    scalePowerMeter(scale, &adj, range);
    pmScaleDraw(0, adj, range); pmGraph(0, fwdPowerMw, 0, scale);
    pmScaleDraw(1, adj, range); pmGraph(1, refPowerMw, 0, scale);
  }
  else
  {
    double bar_mw = (R.mode_display == POWER_CLEAN_DBM) ? netPowerMw : peakPowerMw;
    double scale = scale_ranges(pepPowerMw);
    scalePowerMeter(scale, &adj, range);
    pmScaleDraw(0, adj, range);
    pmGraph(0, bar_mw, pepPowerMw, scale);
  }
  swrScaleDraw();
  swrGraph(swr_mid, swr_alm, swrAvg);
}

static void mainDrawText(void)
{
  char b[28];
  uint16_t lblCol = adcPresent ? C_YELLOW : C_ORANGE;   // orange label = MOCK
  uint16_t bigCol = swrAlarm ? C_RED : C_YELLOW;

  if (R.mode_display == MODULATIONSCOPE)
  {
    tft.setTextFont(4); tft.setTextDatum(TL_DATUM);
    print_p_mw(pepPowerMw);
    snprintf(b, sizeof(b), "PEP:%s", fmtBuf);
    tft.setTextColor(C_YELLOW, C_BLACK); tft.setTextPadding(150);
    tft.drawString(b, MX, 156);
    print_swr(swrAvg);
    snprintf(b, sizeof(b), "SWR:%s", fmtBuf);
    tft.setTextColor(swrAlarm ? C_RED : C_GREEN, C_BLACK); tft.setTextPadding(150);
    tft.setTextDatum(TR_DATUM); tft.drawString(b, MX + MLEN, 156);
    tft.setTextDatum(TL_DATUM); tft.setTextPadding(0);
    return;
  }

  if (R.mode_display == POWER_CLEAN_DBM)
  {
    tft.setTextFont(4); tft.setTextDatum(TL_DATUM);
    tft.setTextColor(lblCol, C_BLACK); tft.setTextPadding(0);
    tft.drawString("Power in dB:", MX, 92);
    print_dbm((int16_t)(10 * peakPowerDb));
    tft.setTextColor(bigCol, C_BLACK);
    tft.setTextDatum(TR_DATUM); tft.setTextPadding(150);
    tft.drawString(fmtBuf, MX + MLEN, 120);
    print_swr(swrAvg);
    snprintf(b, sizeof(b), "SWR:%s", fmtBuf);
    tft.setTextColor(swrAlarm ? C_RED : C_WHITE, C_BLACK);
    tft.setTextDatum(TL_DATUM); tft.setTextPadding(140);
    tft.drawString(b, MX, 150);
    print_dbm((int16_t)(10 * pepPowerDb));
    snprintf(b, sizeof(b), "PEP:%s", fmtBuf);
    tft.setTextColor(C_YELLOW, C_BLACK);
    tft.setTextDatum(TR_DATUM); tft.setTextPadding(150);
    tft.drawString(b, MX + MLEN, 150);
    tft.setTextDatum(TL_DATUM); tft.setTextPadding(0);
    return;
  }

  bool mixed = (R.mode_display == POWER_MIXED);
  int16_t ylabel = mixed ? 100 : 86;
  const char *label = mixed ? "Fwd:" : "Pk:";

  // label
  tft.setTextFont(4); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(lblCol, C_BLACK); tft.setTextPadding(80);
  tft.drawString(label, MX, ylabel + 6);

  // big number
  double disp = mixed ? fwdPowerMw : peakPowerMw;
  print_p_mw(disp);
  tft.setTextFont(4); tft.setTextSize(2);
  tft.setTextColor(bigCol, C_BLACK);
  tft.setTextDatum(TR_DATUM); tft.setTextPadding(228);
  tft.drawString(fmtBuf, MX + MLEN, ylabel);
  tft.setTextSize(1);

  // reverse flag
  tft.setTextFont(2); tft.setTextDatum(TL_DATUM); tft.setTextPadding(44);
  tft.setTextColor(C_RED, C_BLACK);
  tft.drawString(Reverse ? "REV" : " ", MX, ylabel + 34);

  // bottom line
  int16_t ybot = 150;
  print_swr(swrAvg);
  snprintf(b, sizeof(b), "SWR:%s", fmtBuf);
  tft.setTextFont(4);
  tft.setTextColor(swrAlarm ? C_RED : C_WHITE, C_BLACK);
  tft.setTextDatum(TL_DATUM); tft.setTextPadding(130);
  tft.drawString(b, MX, ybot);

  if (mixed) { print_p_mw(refPowerMw); snprintf(b, sizeof(b), "Ref:%s", fmtBuf); }
  else       { print_p_mw(pepPowerMw); snprintf(b, sizeof(b), "PEP:%s", fmtBuf); }
  tft.setTextColor(C_YELLOW, C_BLACK);
  tft.setTextDatum(TR_DATUM); tft.setTextPadding(160);
  tft.drawString(b, MX + MLEN, ybot);
  tft.setTextDatum(TL_DATUM); tft.setTextPadding(0);
}

//*********************************************************************************
//  MODE INTRO  (shown briefly when the display mode changes, like the original)
//*********************************************************************************
static void showModeIntro(void)
{
  tft.fillScreen(C_BLACK);
  tft.setTextDatum(MC_DATUM); tft.setTextPadding(0);
  tft.setTextFont(2); tft.setTextColor(C_WHITE, C_BLACK);
  tft.drawString("Mode:", SCREEN_W / 2, 60);
  tft.setTextFont(4); tft.setTextColor(C_YELLOW, C_BLACK);
  tft.drawString(modeName1[R.mode_display], SCREEN_W / 2, 100);
  tft.drawString(modeName2[R.mode_display], SCREEN_W / 2, 130);
  if (!adcPresent) {
    tft.setTextFont(2); tft.setTextColor(C_ORANGE, C_BLACK);
    tft.drawString("** MOCK 80W / SWR 1.5 **", SCREEN_W / 2, 180);
  }
  tft.setTextDatum(TL_DATUM);
  uiState = UI_INTRO; introEndTime = millis() + MODE_INTRO_MS;
}

//*********************************************************************************
//  PERSISTENCE
//*********************************************************************************
static void savePrefs(void)
{
  prefs.putUChar("det",  R.detector);
  prefs.putUChar("mode", R.mode_display);
  prefs.putUChar("pep",  R.pep_idx);
  prefs.putUChar("cpl",  R.coupler);
  prefs.putUChar("band", R.band);
  prefs.putUChar("almt", R.SWR_alarm_trig);
  prefs.putUShort("almp", R.SWR_alarm_pwr_thresh);
  prefs.putUChar("sr0", R.ScaleRange[0]);
  prefs.putUChar("sr1", R.ScaleRange[1]);
  prefs.putUChar("sr2", R.ScaleRange[2]);
}
static void saveCalPrefs(void)
{
  prefs.putShort("c0db", R.cal_AD[0].db10m);
  prefs.putFloat("c0f",  R.cal_AD[0].Fwd);
  prefs.putFloat("c0r",  R.cal_AD[0].Rev);
  prefs.putShort("c1db", R.cal_AD[1].db10m);
  prefs.putFloat("c1f",  R.cal_AD[1].Fwd);
  prefs.putFloat("c1r",  R.cal_AD[1].Rev);
  prefs.putFloat("mcal", R.meter_cal);
}

//*********************************************************************************
//  CONFIG MENU  -  ESP32-style navigation chrome
//  (UP / ENTER / DOWN buttons on the left, EXIT bottom-right, scrolling item
//   list on the right.  Larger fonts than the small RA8875 port.)
//*********************************************************************************
static void runCalScreen(void);          // defined further below
static void runDebugScreen(void);        // defined further below
static bool readTouch(int &x, int &y);   // defined further below

// ---- navigation buttons ( 0=UP 1=ENTER 2=DOWN 3=EXIT ) ----
struct NavBtn { int16_t x, y, w, h; const char *label; };
static NavBtn navBtn[4] = {
  {   4,  38, 76, 48, "UP"    },
  {   4,  90, 76, 48, "ENTER" },
  {   4, 142, 76, 48, "DOWN"  },
  { 240, 190, 76, 44, "EXIT"  }
};

static void drawNavButtons(void)
{
  for (uint8_t i = 0; i < 4; i++) {
    uint16_t bg = (i == 3) ? C_BTNACT : C_BTNBG;
    tft.fillRoundRect(navBtn[i].x, navBtn[i].y, navBtn[i].w, navBtn[i].h, 6, bg);
    tft.drawRoundRect(navBtn[i].x, navBtn[i].y, navBtn[i].w, navBtn[i].h, 6, C_DKGREY);
    tft.setTextFont(2); tft.setTextColor(C_WHITE, bg);
    tft.setTextDatum(MC_DATUM); tft.setTextPadding(0);
    tft.drawString(navBtn[i].label, navBtn[i].x + navBtn[i].w / 2, navBtn[i].y + navBtn[i].h / 2);
  }
  tft.setTextDatum(TL_DATUM);
}

// Blocks until a nav button is tapped. Keeps draining the sampler so the
// background task buffers stay healthy while the (blocking) menu is open.
static int waitNav(void)
{
  int x, y;
  for (;;) {
    pswr_sync_from_sampler();
    if (readTouch(x, y)) {
      for (uint8_t i = 0; i < 4; i++)
        if (x >= navBtn[i].x && x <= navBtn[i].x + navBtn[i].w &&
            y >= navBtn[i].y && y <= navBtn[i].y + navBtn[i].h) {
          // brief press feedback
          uint16_t bg = (i == 3) ? C_BTNACT : C_BTNBG;
          tft.fillRoundRect(navBtn[i].x, navBtn[i].y, navBtn[i].w, navBtn[i].h, 6, C_LTBLUE);
          tft.drawRoundRect(navBtn[i].x, navBtn[i].y, navBtn[i].w, navBtn[i].h, 6, C_DKGREY);
          tft.setTextFont(2); tft.setTextColor(C_BLACK, C_LTBLUE);
          tft.setTextDatum(MC_DATUM); tft.setTextPadding(0);
          tft.drawString(navBtn[i].label, navBtn[i].x + navBtn[i].w / 2, navBtn[i].y + navBtn[i].h / 2);
          tft.setTextDatum(TL_DATUM);
          while (touch.touched()) { pswr_sync_from_sampler(); delay(10); }
          delay(20);
          tft.fillRoundRect(navBtn[i].x, navBtn[i].y, navBtn[i].w, navBtn[i].h, 6, bg);
          tft.drawRoundRect(navBtn[i].x, navBtn[i].y, navBtn[i].w, navBtn[i].h, 6, C_DKGREY);
          tft.setTextFont(2); tft.setTextColor(C_WHITE, bg);
          tft.setTextDatum(MC_DATUM); tft.setTextPadding(0);
          tft.drawString(navBtn[i].label, navBtn[i].x + navBtn[i].w / 2, navBtn[i].y + navBtn[i].h / 2);
          tft.setTextDatum(TL_DATUM);
          return i;
        }
    }
    delay(15);
  }
}

static void menuTitle(const char *t)
{
  tft.fillScreen(C_BLACK);
  tft.setTextDatum(TL_DATUM); tft.setTextPadding(0);
  tft.setTextFont(4); tft.setTextColor(C_YELLOW, C_BLACK);
  tft.drawString(t, 88, 8);
  drawNavButtons();
}

// List geometry (right of the nav buttons). Font 2 rows, larger than the ESP32 original.
#define LIST_X    88
#define LIST_W    150
#define LIST_Y0   40
#define LIST_RH   22

// Scrolling list selector. Returns chosen index, or -1 on EXIT.
static int menuList(const char *title, const char **items, int n, int start)
{
  int sel = start;
  if (sel < 0 || sel >= n) sel = 0;
  menuTitle(title);
  int top = 0;
  const int visible = (SCREEN_H - LIST_Y0) / LIST_RH;   // rows that fit
  for (;;) {
    if (sel < top) top = sel;
    if (sel >= top + visible) top = sel - visible + 1;

    tft.setTextDatum(TL_DATUM);
    for (int row = 0; row < visible; row++) {
      int i  = top + row;
      int yy = LIST_Y0 + row * LIST_RH;
      bool on = (i == sel);
      tft.fillRect(LIST_X - 4, yy, LIST_W, LIST_RH, on ? C_MENUHI : C_BLACK);
      if (i < n) {
        tft.setTextFont(2);
        tft.setTextColor(on ? C_BLACK : C_GREY, on ? C_MENUHI : C_BLACK);
        tft.setTextPadding(0);
        tft.drawString(items[i], LIST_X, yy + 3);
      }
    }
    int k = waitNav();
    if      (k == 0) sel = (sel - 1 + n) % n;
    else if (k == 2) sel = (sel + 1) % n;
    else if (k == 1) return sel;
    else             return -1;
  }
}

// Big-number integer adjuster. UP increases, DOWN decreases; ENTER/EXIT confirm.
//  tenths : render value as v/10 . v%10  (e.g. 30 -> "3.0")
//  topLbl : if non-NULL and value == hi, show this string instead of the number
static int menuAdjust(const char *title, const char *unit, int val,
                      int lo, int hi, int step, bool tenths, const char *topLbl)
{
  menuTitle(title);
  for (;;) {
    tft.fillRect(86, 64, SCREEN_W - 86, 84, C_BLACK);
    char b[16];
    bool isText = (topLbl && val >= hi);
    if (isText)        snprintf(b, sizeof(b), "%s", topLbl);
    else if (tenths)   snprintf(b, sizeof(b), "%d.%d", val / 10, val % 10);
    else               snprintf(b, sizeof(b), "%d", val);

    tft.setTextFont(isText ? 4 : 6);   // font 6 = big digits only; use font 4 for text
    tft.setTextColor(C_YELLOW, C_BLACK);
    tft.setTextDatum(MC_DATUM); tft.setTextPadding(0);
    tft.drawString(b, 88 + (SCREEN_W - 88) / 2, 96);
    if (unit && unit[0]) {
      tft.setTextFont(2); tft.setTextColor(C_GREY, C_BLACK);
      tft.drawString(unit, 88 + (SCREEN_W - 88) / 2, 138);
    }
    tft.setTextDatum(TL_DATUM);

    int k = waitNav();
    if      (k == 0) { val += step; if (val > hi) val = hi; }
    else if (k == 2) { val -= step; if (val < lo) val = lo; }
    else             return val;     // ENTER or EXIT both confirm
  }
}

//*********************************************************************************
//  CONFIG MENU LOGIC  -  same items / semantics as the working tap-menu,
//  now driven through the ESP32-style navigation chrome above.
//*********************************************************************************
static const char *MENU_TOP[] = {
  "SWR Alarm", "SWR Alarm Power", "PEP Period", "Scale Ranges",
  "Detector", "Calibrate", "Debug Display", "Reset to Default", "Exit"
};
#define MENU_TOP_N  (int)(sizeof(MENU_TOP) / sizeof(MENU_TOP[0]))

static const uint16_t swrPwrSteps[]  = { 1, 5, 10, 25, 50, 100, 200, 500 };
#define SWR_PWR_N  (int)(sizeof(swrPwrSteps) / sizeof(swrPwrSteps[0]))
static const uint8_t  scalePresets[4][3] = { {11,22,55}, {10,20,50}, {15,30,60}, {12,24,60} };

static void menuSwrAlarm(void)
{
  // 1.0 .. 3.9 in 0.1 steps, then "Off" (stored as 40)
  int v = R.SWR_alarm_trig;
  if (v < 10) v = 10; if (v > 40) v = 40;
  v = menuAdjust("SWR Alarm", "x.x : 1   (max = Off)", v, 10, 40, 1, true, "Off");
  R.SWR_alarm_trig = v; savePrefs();
}

static void menuSwrAlarmPower(void)
{
  const char *items[SWR_PWR_N];
  char bufs[SWR_PWR_N][12];
  int start = 0;
  for (int i = 0; i < SWR_PWR_N; i++) {
    snprintf(bufs[i], sizeof(bufs[i]), "%u mW", swrPwrSteps[i]);
    items[i] = bufs[i];
    if (swrPwrSteps[i] == R.SWR_alarm_pwr_thresh) start = i;
  }
  int s = menuList("SWR Alarm Power", items, SWR_PWR_N, start);
  if (s >= 0) { R.SWR_alarm_pwr_thresh = swrPwrSteps[s]; savePrefs(); }
}

static void menuPepPeriod(void)
{
  int s = menuList("PEP Period", PEP_LABELS, 3, R.pep_idx);
  if (s >= 0) { R.pep_idx = (uint8_t)s; savePrefs(); }
}

static void menuScaleRanges(void)
{
  const char *items[4];
  char bufs[4][14];
  int start = 0;
  for (int i = 0; i < 4; i++) {
    snprintf(bufs[i], sizeof(bufs[i]), "%u / %u / %u",
             scalePresets[i][0], scalePresets[i][1], scalePresets[i][2]);
    items[i] = bufs[i];
    if (scalePresets[i][0] == R.ScaleRange[0] &&
        scalePresets[i][1] == R.ScaleRange[1] &&
        scalePresets[i][2] == R.ScaleRange[2]) start = i;
  }
  int s = menuList("Scale Ranges", items, 4, start);
  if (s >= 0) {
    R.ScaleRange[0] = scalePresets[s][0];
    R.ScaleRange[1] = scalePresets[s][1];
    R.ScaleRange[2] = scalePresets[s][2];
    savePrefs();
  }
}

static void menuDetector(void)
{
  const char *items[] = { "2x AD8307 (log)", "Diode / Bruene" };
  int s = menuList("Detector", items, 2, R.detector == DET_AD8307 ? 0 : 1);
  if (s >= 0) { R.detector = (s == 0) ? DET_AD8307 : DET_DIODE; savePrefs(); }
}

static void menuResetDefaults(void)
{
  const char *yn[] = { "No  -  keep settings", "Yes  -  reset all" };
  int s = menuList("Reset to Default", yn, 2, 0);
  if (s == 1) {
    R.detector = DETECTOR_DEFAULT; R.mode_display = POWER_BARPEP; R.pep_idx = 0;
    R.coupler = 1; R.band = 0;
    R.SWR_alarm_trig = SWR_ALARM_DEFAULT; R.SWR_alarm_pwr_thresh = SWR_THRESHOLD_DEFAULT;
    R.ScaleRange[0]=SCALE_RANGE1; R.ScaleRange[1]=SCALE_RANGE2; R.ScaleRange[2]=SCALE_RANGE3;
    R.cal_AD[0].db10m=CAL1_NOR_VALUE; R.cal_AD[0].Fwd=CALFWD1_DEFAULT; R.cal_AD[0].Rev=CALREV1_DEFAULT;
    R.cal_AD[1].db10m=CAL2_NOR_VALUE; R.cal_AD[1].Fwd=CALFWD2_DEFAULT; R.cal_AD[1].Rev=CALREV2_DEFAULT;
    R.meter_cal = METER_CAL;
    savePrefs(); saveCalPrefs();
    menuTitle("Reset to Default");
    tft.setTextFont(4); tft.setTextColor(C_GREEN, C_BLACK);
    tft.setTextDatum(MC_DATUM); tft.setTextPadding(0);
    tft.drawString("Defaults restored", 88 + (SCREEN_W - 88) / 2, 110);
    tft.setTextDatum(TL_DATUM);
    delay(1200);
  }
}

static void enterConfigMenu(void)
{
  int sel = 0;
  for (;;) {
    sel = menuList("CONFIG", MENU_TOP, MENU_TOP_N, sel);
    if (sel < 0 || sel == MENU_TOP_N - 1) break;       // EXIT
    switch (sel) {
      case 0: menuSwrAlarm();       break;
      case 1: menuSwrAlarmPower();  break;
      case 2: menuPepPeriod();      break;
      case 3: menuScaleRanges();    break;
      case 4: menuDetector();       break;
      case 5: runCalScreen();       break;
      case 6: runDebugScreen();     break;
      case 7: menuResetDefaults();  break;
    }
  }
}

//*********************************************************************************
//  CALIBRATION SCREEN
//   AD8307 : One-Level (1 point + 2nd point derived -30 dB) and Two-Level
//            (SET P1 / SET P2), with a 12 dB Fwd/Rev quality gate, exactly as
//            the original PSWRmenu.ino.
//   Diode  : one-point cal (not in the original, which is compile-time only):
//            meter_cal_new = meter_cal * sqrt(P_known / P_measured)
//*********************************************************************************
int16_t calRefDb10m = CAL1_NOR_VALUE;
float   calRefWatts = 80.0;
#define CAL_QUAL_VOLTS  (CAL_INP_QUALITY * LOGAMP_SLOPE * 0.001)
#define CAL_BAD 0
#define CAL_FWD 1
#define CAL_REV 2

Btn calbtn[8];
uint8_t numCalButtons = 0;
const char *calBtnLabels[8];

static uint8_t calQuality(void)
{
  if      ((v_fwd - v_rev) > CAL_QUAL_VOLTS) return CAL_FWD;
  else if ((v_rev - v_fwd) > CAL_QUAL_VOLTS) return CAL_REV;
  return CAL_BAD;
}
static void drawCalBtn(uint8_t i, uint16_t fill)
{
  tft.fillRoundRect(calbtn[i].x, calbtn[i].y, calbtn[i].w, calbtn[i].h, 6, fill);
  tft.drawRoundRect(calbtn[i].x, calbtn[i].y, calbtn[i].w, calbtn[i].h, 6, C_WHITE);
  tft.setTextFont(2); tft.setTextColor(C_WHITE, fill);
  tft.setTextDatum(MC_DATUM); tft.setTextPadding(0);
  tft.drawString(calBtnLabels[i], calbtn[i].x + calbtn[i].w / 2, calbtn[i].y + calbtn[i].h / 2);
  tft.setTextDatum(TL_DATUM);
}
static bool calInside(uint8_t i, int px, int py)
{ return px >= calbtn[i].x && px <= calbtn[i].x + calbtn[i].w && py >= calbtn[i].y && py <= calbtn[i].y + calbtn[i].h; }

static void calFlash(const char *msg, uint16_t colour)
{
  tft.setTextFont(2); tft.setTextDatum(TC_DATUM);
  tft.setTextColor(colour, C_BLACK); tft.setTextPadding(SCREEN_W - 4);
  tft.drawString(msg, SCREEN_W / 2, 220);
  tft.setTextDatum(TL_DATUM); tft.setTextPadding(0);
}

static void drawCalLive(void)
{
  char b[44];
  tft.setTextFont(2); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_WHITE, C_BLACK); tft.setTextPadding(200);
  snprintf(b, sizeof(b), "Vf: %.4f V   Vr: %.4f V", v_fwd, v_rev);
  tft.drawString(b, 8, 22);

  if (R.detector == DET_AD8307)
  {
    uint8_t q = calQuality();
    tft.setTextDatum(TR_DATUM); tft.setTextPadding(110);
    if      (q == CAL_FWD) { tft.setTextColor(C_GREEN,  C_BLACK); tft.drawString("FWD OK",   SCREEN_W - 8, 22); }
    else if (q == CAL_REV) { tft.setTextColor(C_YELLOW, C_BLACK); tft.drawString("REV",      SCREEN_W - 8, 22); }
    else                   { tft.setTextColor(C_RED,    C_BLACK); tft.drawString("POOR sig", SCREEN_W - 8, 22); }
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(1); tft.setTextColor(C_CYAN, C_BLACK); tft.setTextPadding(SCREEN_W - 16);
    snprintf(b, sizeof(b), "P1: %+.1f dBm  F=%.3f R=%.3f", R.cal_AD[0].db10m / 10.0, R.cal_AD[0].Fwd, R.cal_AD[0].Rev);
    tft.drawString(b, 8, 42);
    snprintf(b, sizeof(b), "P2: %+.1f dBm  F=%.3f R=%.3f", R.cal_AD[1].db10m / 10.0, R.cal_AD[1].Fwd, R.cal_AD[1].Rev);
    tft.drawString(b, 8, 52);
    tft.setTextFont(4); tft.setTextDatum(TC_DATUM); tft.setTextColor(C_YELLOW, C_BLACK); tft.setTextPadding(180);
    snprintf(b, sizeof(b), "Ref: %+.1f dBm", calRefDb10m / 10.0);
    tft.drawString(b, SCREEN_W / 2, 64);
  }
  else
  {
    print_p_mw(fwdPowerMw);
    tft.setTextColor(C_GREEN, C_BLACK); tft.setTextDatum(TR_DATUM); tft.setTextPadding(120);
    snprintf(b, sizeof(b), "Meas: %s", fmtBuf);
    tft.drawString(b, SCREEN_W - 8, 22); tft.setTextDatum(TL_DATUM);
    tft.setTextFont(1); tft.setTextColor(C_CYAN, C_BLACK); tft.setTextPadding(SCREEN_W - 16);
    snprintf(b, sizeof(b), "meter_cal=%.4f  N=%.0f:1  Vdrop=%.2f", R.meter_cal, BRIDGE_COUPLING, D_VDROP);
    tft.drawString(b, 8, 46);
    tft.setTextFont(4); tft.setTextDatum(TC_DATUM); tft.setTextColor(C_YELLOW, C_BLACK); tft.setTextPadding(180);
    snprintf(b, sizeof(b), "Known: %.0f W", calRefWatts);
    tft.drawString(b, SCREEN_W / 2, 64);
  }
  tft.setTextDatum(TL_DATUM); tft.setTextPadding(0);
}

static bool calScreenDone = false;

static void showCalScreen(void)
{
  tft.fillScreen(C_BLACK);
  tft.setTextFont(2); tft.setTextDatum(TC_DATUM); tft.setTextColor(C_WHITE, C_BLACK); tft.setTextPadding(0);
  tft.drawString(R.detector == DET_AD8307 ? "CALIBRATE - 2x AD8307" : "CALIBRATE - Diode (1 point)", SCREEN_W / 2, 2);
  tft.setTextDatum(TL_DATUM);

  int gap = 6, bw4 = (SCREEN_W - 5 * gap) / 4, y1 = 96, h = 36;
  for (uint8_t i = 0; i < 4; i++) { calbtn[i].x = gap + i * (bw4 + gap); calbtn[i].y = y1; calbtn[i].w = bw4; calbtn[i].h = h; calbtn[i].color = C_NAVY; }
  calBtnLabels[0] = "-10"; calBtnLabels[1] = "-1"; calBtnLabels[2] = "+1"; calBtnLabels[3] = "+10";

  int y2 = 140;
  if (R.detector == DET_AD8307)
  {
    int bw3 = (SCREEN_W - 4 * gap) / 3;
    calbtn[4] = { gap,               y2, bw3, h, 0 }; calBtnLabels[4] = "1-LEVEL";
    calbtn[5] = { gap*2 + bw3,       y2, bw3, h, 0 }; calBtnLabels[5] = "SET P1";
    calbtn[6] = { gap*3 + bw3*2,     y2, bw3, h, 0 }; calBtnLabels[6] = "SET P2";
    calbtn[7] = { gap, 184, SCREEN_W - 2*gap, 32, 0 }; calBtnLabels[7] = "BACK";
    calbtn[4].color = 0x04A0; calbtn[5].color = 0x6180; calbtn[6].color = 0x6180; calbtn[7].color = 0x2104;
    numCalButtons = 8;
  }
  else
  {
    int bw2 = (SCREEN_W - 3 * gap) / 2;
    calbtn[4] = { gap,         y2, bw2, h, 0x04A0 }; calBtnLabels[4] = "SET CAL";
    calbtn[5] = { gap*2 + bw2, y2, bw2, h, 0x8000 }; calBtnLabels[5] = "RESET";
    calbtn[6] = { gap, 184, SCREEN_W - 2*gap, 32, 0x2104 }; calBtnLabels[6] = "BACK";
    numCalButtons = 7;
  }
  for (uint8_t i = 0; i < 4; i++) drawCalBtn(i, calbtn[i].color);
  for (uint8_t i = 4; i < numCalButtons; i++) drawCalBtn(i, calbtn[i].color);
  drawCalLive();
  calFlash(R.detector == DET_AD8307 ? "Apply carrier at Ref level, then SET"
                                    : "Carrier into dummy load, set W, SET CAL", C_DKGREY);
}

static void handleCalBtn(uint8_t i)
{
  drawCalBtn(i, C_LTBLUE); delay(90); drawCalBtn(i, calbtn[i].color);

  if (i < 4) {
    const int16_t sdb[4] = { -100, -10, 10, 100 };
    const float   sw[4]  = { -10, -1, 1, 10 };
    if (R.detector == DET_AD8307)
      calRefDb10m = constrain(calRefDb10m + sdb[i], CAL_DBM_MIN, CAL_DBM_MAX);
    else { calRefWatts += sw[i]; if (calRefWatts < 1) calRefWatts = 1; if (calRefWatts > 2000) calRefWatts = 2000; }
    drawCalLive(); return;
  }

  if (R.detector == DET_AD8307)
  {
    if (i == 7) { calScreenDone = true; return; }
    uint8_t q = calQuality();
    if (q == CAL_BAD) { calFlash("Poor signal - nothing stored", C_RED); return; }
    if (i == 4) {                                   // 1-LEVEL
      if (q == CAL_FWD) {
        R.cal_AD[0].db10m = calRefDb10m; R.cal_AD[0].Fwd = v_fwd; R.cal_AD[0].Rev = v_fwd;
        R.cal_AD[1].db10m = R.cal_AD[0].db10m - 300;
        R.cal_AD[1].Fwd = R.cal_AD[0].Fwd - LOGAMP_SLOPE * 0.001 * 30;
        R.cal_AD[1].Rev = R.cal_AD[0].Rev - LOGAMP_SLOPE * 0.001 * 30;
      } else {
        R.cal_AD[0].db10m = calRefDb10m; R.cal_AD[0].Rev = v_rev;
        R.cal_AD[1].db10m = R.cal_AD[0].db10m - 300;
        R.cal_AD[1].Rev = R.cal_AD[0].Rev - LOGAMP_SLOPE * 0.001 * 30;
      }
      saveCalPrefs();
      calFlash(q == CAL_FWD ? "1-Level stored (Fwd+Rev, P2 -30dB)" : "1-Level stored (Rev, P2 -30dB)", C_GREEN);
    } else if (i == 5 || i == 6) {                  // SET P1 / P2
      uint8_t p = (i == 5) ? 0 : 1;
      R.cal_AD[p].db10m = calRefDb10m;
      if (q == CAL_FWD) { R.cal_AD[p].Fwd = v_fwd; R.cal_AD[p].Rev = v_fwd; }
      else              { R.cal_AD[p].Rev = v_rev; }
      saveCalPrefs();
      if (fabs(R.cal_AD[0].Fwd - R.cal_AD[1].Fwd) < 0.05) calFlash("Stored - WARNING P1/P2 too close!", C_ORANGE);
      else calFlash(p == 0 ? "Point 1 stored" : "Point 2 stored", C_GREEN);
    }
    drawCalLive(); return;
  }

  // diode
  if (i == 6) { calScreenDone = true; return; }
  if (i == 4) {
    if (fwdPowerMw < 100.0) { calFlash("No carrier - apply power first", C_RED); return; }
    float ratio = sqrt((calRefWatts * 1000.0) / fwdPowerMw);
    float nc = R.meter_cal * ratio;
    if (nc < 0.1) nc = 0.1; if (nc > 10.0) nc = 10.0;
    R.meter_cal = nc; saveCalPrefs();
    char b[40]; snprintf(b, sizeof(b), "Stored: meter_cal = %.4f", R.meter_cal);
    calFlash(b, C_GREEN);
  } else if (i == 5) { R.meter_cal = 1.0; saveCalPrefs(); calFlash("meter_cal reset to 1.0000", C_YELLOW); }
  drawCalLive();
}

//*********************************************************************************
//  CALIBRATION SCREEN DRIVER  (blocking, reached from the config menu)
//*********************************************************************************
static void runCalScreen(void)
{
  calScreenDone = false;
  showCalScreen();
  uint32_t lastDraw = 0;
  int x, y;
  while (!calScreenDone) {
    pswr_sync_from_sampler();
    calc_SWR_and_power();
    if (millis() - lastDraw >= 120) { lastDraw = millis(); drawCalLive(); }
    if (readTouch(x, y)) {
      for (uint8_t i = 0; i < numCalButtons; i++)
        if (calInside(i, x, y)) { handleCalBtn(i); break; }
      while (touch.touched()) { pswr_sync_from_sampler(); delay(10); }
    }
    delay(10);
  }
}

//*********************************************************************************
//  DEBUG / RAW SCREEN  (original "Debug Display")
//*********************************************************************************
static void drawRawScreen(void)
{
  char b[44];
  tft.setTextDatum(TL_DATUM); tft.setTextFont(2);
  tft.setTextColor(C_WHITE, C_BLACK); tft.setTextPadding(280);
  tft.drawString("DEBUG - RAW DETECTOR VOLTAGES", 10, 6);
  tft.setTextFont(4); tft.setTextColor(C_GREEN, C_BLACK);
  snprintf(b, sizeof(b), "Vfwd: %.4f V", v_fwd); tft.drawString(b, 10, 36);
  tft.setTextColor(C_PINK, C_BLACK);
  snprintf(b, sizeof(b), "Vrev: %.4f V", v_rev); tft.drawString(b, 10, 66);
  tft.setTextFont(2); tft.setTextColor(C_CYAN, C_BLACK);
  if (R.detector == DET_AD8307) {
    snprintf(b, sizeof(b), "Fwd %.1f dBm   Rev %.1f dBm", ad8307FwdDbm, ad8307RevDbm); tft.drawString(b, 10, 102);
  } else {
    snprintf(b, sizeof(b), "Coupling %.0f:1  Vdrop %.2f V  cal %.3f", BRIDGE_COUPLING, D_VDROP, R.meter_cal); tft.drawString(b, 10, 102);
  }
  snprintf(b, sizeof(b), "Pwr %.1f mW   SWR %.2f", netPowerMw, swr); tft.drawString(b, 10, 122);
  tft.setTextColor(adcPresent ? C_GREEN : C_ORANGE, C_BLACK);
  tft.drawString(adcPresent ? "ADS1015 detected on I2C 0x48" : "ADS1015 NOT found - mock 80W/1.5", 10, 150);
  tft.setTextColor(C_DKGREY, C_BLACK);
  tft.drawString("Tap anywhere to return to menu", 10, 200);
  tft.setTextPadding(0);
}

static void runDebugScreen(void)
{
  tft.fillScreen(C_BLACK);
  uint32_t lastDraw = 0;
  int x, y;
  for (;;) {
    pswr_sync_from_sampler();
    calc_SWR_and_power();
    if (millis() - lastDraw >= 150) { lastDraw = millis(); drawRawScreen(); }
    if (readTouch(x, y)) { while (touch.touched()) { pswr_sync_from_sampler(); delay(10); } return; }
    delay(10);
  }
}

//*********************************************************************************
//  TOUCH
//*********************************************************************************
static bool readTouch(int &x, int &y)
{
  if (!touch.tirqTouched() || !touch.touched()) return false;
  TS_Point p = touch.getPoint();
  if (p.z < TOUCH_MIN_PRESSURE) return false;
  x = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_W);
  y = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_H);
  x = constrain(x, 0, SCREEN_W); y = constrain(y, 0, SCREEN_H);
  return true;
}
static void waitTouchRelease(void) { while (touch.touched()) delay(10); delay(30); }

static void handleFrontPanel(uint8_t i)
{
  fpDraw(i, C_BTNACT); delay(90); fpDraw(i, fp[i].color);
  switch (i) {
    case 0: R.coupler = (R.coupler == 1) ? 2 : 1; savePrefs(); fpDraw(0, fp[0].color); break;
    case 1: R.band = (R.band + 1) & 3; savePrefs(); fpDraw(1, fp[1].color); break;
    case 2: R.pep_idx = (R.pep_idx + 1) % 3; savePrefs(); fpDraw(2, fp[2].color); break;
    case 3: R.mode_display++; if (R.mode_display > MAX_MODE) R.mode_display = 1; savePrefs(); showModeIntro(); break;
    case 4: enterConfigMenu(); uiNeedsRedraw = true; uiState = UI_MAIN; break;
  }
}

static void pollTouch(void)
{
  int x, y;
  if (!readTouch(x, y)) return;
  switch (uiState) {
    case UI_MAIN:
      for (uint8_t i = 0; i < FP_N; i++) if (fpInside(i, x, y)) { handleFrontPanel(i); break; }
      break;
    case UI_INTRO: introEndTime = 0; break;
    default: break;
  }
  waitTouchRelease();
}

//*********************************************************************************
//  BOOT
//*********************************************************************************
static void bootScreen(void)
{
  tft.fillScreen(C_BLACK);
  tft.setTextDatum(MC_DATUM); tft.setTextPadding(0);
  tft.setTextFont(4); tft.setTextColor(C_YELLOW, C_BLACK);
  tft.drawString("You're welcome.", SCREEN_W / 2, 86);
  tft.setTextFont(2); tft.setTextColor(C_CYAN, C_BLACK);
  tft.drawString("Power & SWR Meter", SCREEN_W / 2, 128);
  tft.drawString("TF3LJ / VE2AO  -  ESP32 CYD port", SCREEN_W / 2, 148);
  tft.setTextColor(C_DKGREY, C_BLACK);
  tft.drawString("v" VERSION, SCREEN_W / 2, 172);
  tft.setTextColor(adcPresent ? C_GREEN : C_ORANGE, C_BLACK);
  tft.drawString(adcPresent ? "ADS1015 detected" : "ADS1015 not found: MOCK 80W / SWR 1.5", SCREEN_W / 2, 202);
  tft.setTextDatum(TL_DATUM);
  delay(2500);
}

//*********************************************************************************
//  SETUP
//*********************************************************************************
void setup()
{
  pinMode(LED_R, OUTPUT); digitalWrite(LED_R, HIGH);
  pinMode(LED_G, OUTPUT); digitalWrite(LED_G, HIGH);
  pinMode(LED_B, OUTPUT); digitalWrite(LED_B, HIGH);

  // defaults
  R.cal_AD[0].db10m = CAL1_NOR_VALUE; R.cal_AD[0].Fwd = CALFWD1_DEFAULT; R.cal_AD[0].Rev = CALREV1_DEFAULT;
  R.cal_AD[1].db10m = CAL2_NOR_VALUE; R.cal_AD[1].Fwd = CALFWD2_DEFAULT; R.cal_AD[1].Rev = CALREV2_DEFAULT;
  R.SWR_alarm_trig = SWR_ALARM_DEFAULT; R.SWR_alarm_pwr_thresh = SWR_THRESHOLD_DEFAULT;
  R.ScaleRange[0] = SCALE_RANGE1; R.ScaleRange[1] = SCALE_RANGE2; R.ScaleRange[2] = SCALE_RANGE3;
  R.meter_cal = METER_CAL; R.coupler = 1; R.band = 0; R.pep_idx = 0;

  prefs.begin("pswr", false);
  R.detector     = prefs.getUChar("det",  DETECTOR_DEFAULT);
  R.mode_display = prefs.getUChar("mode", POWER_BARPEP);
  if (R.mode_display < 1 || R.mode_display > MAX_MODE) R.mode_display = POWER_BARPEP;
  R.pep_idx = prefs.getUChar("pep", 0); if (R.pep_idx > 2) R.pep_idx = 0;
  R.coupler = prefs.getUChar("cpl", 1); if (R.coupler < 1 || R.coupler > 2) R.coupler = 1;
  R.band    = prefs.getUChar("band", 0) & 3;
  R.SWR_alarm_trig = prefs.getUChar("almt", SWR_ALARM_DEFAULT);
  R.SWR_alarm_pwr_thresh = prefs.getUShort("almp", SWR_THRESHOLD_DEFAULT);
  R.ScaleRange[0] = prefs.getUChar("sr0", SCALE_RANGE1);
  R.ScaleRange[1] = prefs.getUChar("sr1", SCALE_RANGE2);
  R.ScaleRange[2] = prefs.getUChar("sr2", SCALE_RANGE3);
  R.cal_AD[0].db10m = prefs.getShort("c0db", R.cal_AD[0].db10m);
  R.cal_AD[0].Fwd   = prefs.getFloat("c0f",  R.cal_AD[0].Fwd);
  R.cal_AD[0].Rev   = prefs.getFloat("c0r",  R.cal_AD[0].Rev);
  R.cal_AD[1].db10m = prefs.getShort("c1db", R.cal_AD[1].db10m);
  R.cal_AD[1].Fwd   = prefs.getFloat("c1f",  R.cal_AD[1].Fwd);
  R.cal_AD[1].Rev   = prefs.getFloat("c1r",  R.cal_AD[1].Rev);
  R.meter_cal       = prefs.getFloat("mcal", R.meter_cal);
  calRefDb10m     = R.cal_AD[0].db10m;

  tft.init();
  tft.invertDisplay(true);
  tft.setRotation(1);
  tft.fillScreen(C_BLACK);

  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSPI);
  touch.setRotation(1);

  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  adcPresent = adsDetect();

  bootScreen();

  samplerBuf.writeIdx = samplerBuf.readIdx = 0;
  xTaskCreatePinnedToCore(samplerTask, "sampler", 4096, NULL, 2, NULL, 0);

  fpSetup();
  showModeIntro();
}

//*********************************************************************************
//  LOOP  -  core 1
//*********************************************************************************
void loop()
{
  static uint32_t lastPoll = 0;
  static bool alt = false;

  pswr_sync_from_sampler();

  uint32_t now = millis();
  if (now - lastPoll < POLL_MS) return;
  lastPoll = now;

  calc_SWR_and_power();

  digitalWrite(LED_G, powerDetected ? LOW : HIGH);
  if (swrAlarm) digitalWrite(LED_R, LOW);

  switch (uiState)
  {
    case UI_INTRO:
      if (millis() >= introEndTime) { uiNeedsRedraw = true; uiState = UI_MAIN; }
      break;

    case UI_MAIN:
      if (uiNeedsRedraw) { mainScreenInit(); uiNeedsRedraw = false; alt = false; }
      if (R.mode_display == MODULATIONSCOPE) {
        scopeFullscale = scale_ranges(pepPowerMw);          // mW fullscale
        scopeAdd(netPowerMw, scopeFullscale);                   // feed envelope
        if (alt) mainDrawGraphs(); else mainDrawText();
      } else {
        if (alt) mainDrawGraphs(); else mainDrawText();
      }
      alt = !alt;
      break;

    default:
      break;
  }

  pollTouch();
}

//*********************************************************************************
//  Arduino.h  -  Just enough of the Arduino core to host-compile
//  Ads1015Detector.cpp, which only uses delayMicroseconds() from it.  Picked up
//  via -I ahead of the real thing because there is no real thing on native.
//*********************************************************************************
#pragma once

#include <stdint.h>

inline void delayMicroseconds(unsigned int) {}

// SPDX-FileCopyrightText: 2023 Tim Hawes
//
// SPDX-License-Identifier: MIT

#ifndef LOOP_WATCHDOG_HPP
#define LOOP_WATCHDOG_HPP

#include <Arduino.h>
#include <Ticker.h>

class LoopWatchdog
{
private:
  Ticker ticker;
  void callback();
  unsigned long last_feed;
  long timeout = 30000;
  const uint32_t rtc_offset = 4;
  const uint32_t rtc_magic = 0x971c4cc8;
  bool restarted_flag = false;

public:
  LoopWatchdog();
  void begin();
  void feed();
  bool restarted();
};

#endif

// SPDX-FileCopyrightText: 2019 Tim Hawes
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RELAY_HPP
#define RELAY_HPP

#include <Arduino.h>

class Relay {
 private:
  int pin;
  int active_value = HIGH;
  int inactive_value = LOW;
  bool pin_initialized = false;
  bool state = false;
 public:
  Relay(int pin);
  void setInvert(bool invert);
  void active(bool active);
};

#endif

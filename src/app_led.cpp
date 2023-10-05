// SPDX-FileCopyrightText: 2019-2020 Tim Hawes
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app_led.h"

Led::Led(int _led_pin) {
  led_pin = _led_pin;
  ticker.attach_ms(5, std::bind(&Led::callback, this));
}

void Led::callback() {
  if (pending_change && (long)(millis() - next_change) >= 0) {
    if (is_on) {
      is_on = !is_on;
      analogWrite(led_pin, 0);
      next_change = millis() + off_time;
    } else {
      is_on = !is_on;
      analogWrite(led_pin, bright_level);
      next_change = millis() + on_time;
    }
  }
}

void Led::begin() {
  pinMode(led_pin, OUTPUT);
}

void Led::on() {
  if (mode == MODE_ON) {
    return;
  }
  mode = MODE_ON;
  pending_change = false;
  analogWrite(led_pin, bright_level);
  is_on = true;
}

void Led::off() {
  if (mode == MODE_OFF) {
    return;
  }
  mode = MODE_OFF;
  pending_change = false;
  analogWrite(led_pin, 0);
  is_on = false;
}

void Led::flash_fast() {
  if (mode == MODE_FAST) {
    return;
  }
  mode = MODE_FAST;
  on_time = 40;
  off_time = 40;
  next_change = millis();
  pending_change = true;
}

void Led::flash_medium() {
  if (mode == MODE_MEDIUM) {
    return;
  }
  mode = MODE_MEDIUM;
  on_time = 500;
  off_time = 500;
  next_change = millis();
  pending_change = true;
}

void Led::flash_slow() {
  if (mode == MODE_SLOW) {
    return;
  }
  mode = MODE_SLOW;
  on_time = 1000;
  off_time = 1000;
  next_change = millis();
  pending_change = true;
}

void Led::blink() {
  if (mode == MODE_BLINK) {
    return;
  }
  mode = MODE_BLINK;
  on_time = 25;
  off_time = 1225;
  next_change = millis();
  pending_change = true;
}

void Led::dim() {
  if (mode == MODE_DIM) {
    return;
  }
  mode = MODE_DIM;
  pending_change = false;
  analogWrite(led_pin, dim_level);
}

void Led::setDimLevel(int level) {
  dim_level = level;
  if (mode == MODE_DIM) {
    analogWrite(led_pin, dim_level);
  }
}

void Led::setBrightLevel(int level) {
  bright_level = level;
  if (mode == MODE_ON) {
    analogWrite(led_pin, bright_level);
  }
}

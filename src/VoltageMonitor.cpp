// SPDX-FileCopyrightText: 2019 Tim Hawes
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VoltageMonitor.hpp"

VoltageMonitor::VoltageMonitor() {

}

void VoltageMonitor::begin() {
  ticker.detach();
  ticker_callback();
  ticker.attach_ms(interval, std::bind(&VoltageMonitor::ticker_callback, this));
}

void VoltageMonitor::set_interval(int _interval) {
  if (interval != _interval) {
    interval = _interval;
    ticker.detach();
    ticker.attach_ms(interval, std::bind(&VoltageMonitor::ticker_callback, this));
  }
}

void VoltageMonitor::set_ratio(float ratio) {
  adc_to_volts = ratio;
}

void VoltageMonitor::set_threshold(float battery, float mains) {
  battery_threshold = battery;
  mains_threshold = mains;
}

void VoltageMonitor::ticker_callback() {
  voltage = analogRead(A0) * adc_to_volts;
  if (voltage_callback) {
    voltage_callback(voltage);
  }
  if (voltage < battery_threshold) {
    if (!on_battery) {
      on_battery = true;
      first_run = false;
      if (on_battery_callback) {
        on_battery_callback();
      }
    }
  } else if (voltage > mains_threshold) {
    if (on_battery || first_run) {
      on_battery = false;
      first_run = false;
      if (on_mains_callback) {
        on_mains_callback();
      }
    }
  } else {
    if (first_run) {
      on_battery = false;
      first_run = false;
      if (on_mains_callback) {
        // if voltage is ambigious and this is our first check,
        // assume that we're on mains and generate a callback.
        on_mains_callback();
      }
    }
  }
}

#ifndef VOLTAGEMONITOR_HPP
#define VOLTAGEMONITOR_HPP

#include <Arduino.h>
#include <Ticker.h>

typedef void (*voltagemonitor_voltage_cb_t)(float voltage);
typedef void (*voltagemonitor_cb_t)();

class VoltageMonitor {
 private:
  float adc_to_volts = 0.014;
  float battery_threshold = 13.6;
  float mains_threshold = 13.7;
  float voltage = 13.8;
  int interval = 5000;
  bool on_battery = false;
  bool first_run = true;
  Ticker ticker;
  void ticker_callback();

 public:
  voltagemonitor_cb_t on_battery_callback = NULL;
  voltagemonitor_cb_t on_mains_callback = NULL;
  voltagemonitor_voltage_cb_t voltage_callback = NULL;
  VoltageMonitor();
  void begin();
  void set_interval(int interval);
  void set_ratio(float ratio);
  void set_threshold(float battery, float mains);
};

#endif

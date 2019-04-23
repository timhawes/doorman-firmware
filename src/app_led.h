#ifndef APP_LED_H
#define APP_LED_H

#include <Arduino.h>
#include <Ticker.h>

class Led
{
private:
  Ticker ticker;
  enum { MODE_OFF, MODE_DIM, MODE_BLINK, MODE_SLOW, MODE_MEDIUM, MODE_FAST, MODE_ON } mode = MODE_OFF;
  int led_pin;
  long on_time;
  long off_time;
  bool is_on;
  void callback();

public:
  Led(int led_pin);
  void begin();
  void blink();
  void flash_fast();
  void flash_medium();
  void flash_slow();
  void off();
  void on();
  void dim();
};

#endif

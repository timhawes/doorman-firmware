#include "app_led.h"

Led::Led(int _led_pin) {
  led_pin = _led_pin;
}

void Led::callback() {
  if (is_on) {
    is_on = !is_on;
    analogWrite(led_pin, 0);
    ticker.once_ms(off_time, std::bind(&Led::callback, this));
  } else {
    is_on = !is_on;
    analogWrite(led_pin, 1023);
    ticker.once_ms(on_time, std::bind(&Led::callback, this));
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
  ticker.detach();
  analogWrite(led_pin, 1023);
  is_on = true;
}

void Led::off() {
  if (mode == MODE_OFF) {
    return;
  }
  mode = MODE_OFF;
  ticker.detach();
  analogWrite(led_pin, 0);
  is_on = false;
}

void Led::flash_fast() {
  if (mode == MODE_FAST) {
    return;
  }
  mode = MODE_FAST;
  ticker.detach();
  on_time = 40;
  off_time = 40;
  callback();
}

void Led::flash_medium() {
  if (mode == MODE_MEDIUM) {
    return;
  }
  mode = MODE_MEDIUM;
  ticker.detach();
  on_time = 500;
  off_time = 500;
  callback();
}

void Led::flash_slow() {
  if (mode == MODE_SLOW) {
    return;
  }
  mode = MODE_SLOW;
  ticker.detach();
  on_time = 1000;
  off_time = 1000;
  callback();
}

void Led::blink() {
  if (mode == MODE_BLINK) {
    return;
  }
  mode = MODE_BLINK;
  ticker.detach();
  on_time = 25;
  off_time = 1225;
  callback();
}

void Led::dim() {
  if (mode == MODE_DIM) {
    return;
  }
  mode = MODE_DIM;
  ticker.detach();
  analogWrite(led_pin, 150);
}

#include "Relay.hpp"

Relay::Relay(int _pin) {
  pin = _pin;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, inactive_value);
}

void Relay::setInvert(bool invert) {
  if (invert) {
    active_value = LOW;
    inactive_value = HIGH;
  } else {
    active_value = HIGH;
    inactive_value = LOW;
  }
  if (state) {
    digitalWrite(pin, active_value);
  } else {
    digitalWrite(pin, inactive_value);
  }
}

void Relay::active(bool _active) {
  state = _active;
  if (state) {
    digitalWrite(pin, active_value);
  } else {
    digitalWrite(pin, inactive_value);
  }
}

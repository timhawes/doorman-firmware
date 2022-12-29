// SPDX-FileCopyrightText: 2017-2019 Tim Hawes
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app_util.h"
#include "Wire.h"

int decode_hex(const char *hexstr, uint8_t *bytes, size_t max_len)
{
  if (strlen(hexstr) % 2 != 0) {
    return 0;
  }
  int bytelen = strlen(hexstr) / 2;
  if (bytelen > max_len) {
    return 0;
  }
  for (int i=0; i<bytelen; i++) {
    uint8_t ms = hexstr[i*2];
    uint8_t ls = hexstr[(i*2)+1];
    if (ms >= 'a') {
      ms = ms - 'a' + 10;
    } else if (ms >= 'A') {
      ms = ms - 'A' + 10;
    } else {
      ms = ms - '0';
    }
    if (ls >= 'a') {
      ls = ls - 'a' + 10;
    } else if (ls >= 'A') {
      ls = ls - 'A' + 10;
    } else {
      ls = ls - '0';
    }
    bytes[i] = (ms << 4) | ls;
  }
  return bytelen;
}

String hexlify(uint8_t bytes[], uint8_t len)
{
  String output;
  output.reserve(len*2);
  for (int i=0; i<len; i++) {
    char hex[3];
    sprintf(hex, "%02x", bytes[i]);
    output.concat(hex);
  }
  return output;
}

void i2c_scan()
{
  uint8_t error, address, nDevices;
  Serial.println("scanning i2c devices");
  for(address = 1; address < 127; address++)
  {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0)
    {
      Serial.print("I2C device found at address 0x");
      if (address<16)
        Serial.print("0");
      Serial.print(address,HEX);
      Serial.println("  !");

      nDevices++;
    }
    else if (error==4)
    {
      Serial.print("Unknown error at address 0x");
      if (address<16)
        Serial.print("0");
      Serial.println(address,HEX);
    }
  }
}

MilliClock::MilliClock()
{
  start_time = millis();
  running_time = 0;
  running = false;
}

void MilliClock::reset()
{
  running_time = 0;
  start_time = millis();
}

void MilliClock::start()
{
  if (!running) {
    start_time = millis();
    running = true;
  }
}

void MilliClock::stop()
{
  if (running) {
    running_time = running_time + (millis() - start_time);
    running = false;
  }
}

unsigned long MilliClock::read()
{
  if (running) {
    return running_time + (millis() - start_time);
  } else {
    return running_time;
  }
}

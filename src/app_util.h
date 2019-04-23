#ifndef APP_UTIL_H
#define APP_UTIL_H

#include <Arduino.h>

int decode_hex(const char *hexstr, uint8_t *bytes, size_t max_len);
String hexlify(uint8_t bytes[], uint8_t len);
void i2c_scan();

class MilliClock
{
private:
  unsigned long start_time;
  unsigned long running_time;
  bool running;
public:
  MilliClock();
  unsigned long read();
  void start();
  void stop();
  void reset();
};

#endif

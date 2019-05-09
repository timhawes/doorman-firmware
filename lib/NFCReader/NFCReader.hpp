#ifndef NFCREADER_HPP
#define NFCREADER_HPP

#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>

#include "NFCToken.hpp"

typedef void (*nfctoken_callback_t)(NFCToken);

class NFC
{
private:
  PN532_I2C *_pn532i2c;
  PN532 *_pn532;
  uint8_t _reset_pin;
  bool ready;
  const uint16_t token_present_timeout = 350;
  const uint16_t pn532_check_interval_min = 250;
  const uint16_t pn532_check_interval_max = 10000;
  uint16_t pn532_check_interval = 250;
  NFCToken current;
  void serialHexdump(uint8_t *bytes, int len);
public:
  NFC(PN532_I2C &pn532i2c, PN532 &pn532, uint8_t reset_pin);
  nfctoken_callback_t token_present_callback = NULL;
  nfctoken_callback_t token_removed_callback = NULL;
  bool read_counter = false;
  bool read_sig = false;
  int read_data = 0;
  int reset_count = 0;
  void begin();
  void loop();
  bool debug = false;
};

#endif

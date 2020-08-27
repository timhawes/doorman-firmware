#ifndef NFCREADER_HPP
#define NFCREADER_HPP

#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>

#include "NFCToken.hpp"

typedef void (*nfctoken_callback_t)(NFCToken);
typedef void (*nfcreader_status_callback_t)(bool ready);

class NFC
{
private:
  PN532_I2C *_pn532i2c;
  PN532 *_pn532;
  uint8_t _reset_pin;
  bool ready;
  const uint16_t token_present_timeout = 350;
  int per_5s_count = 0;
  int per_1m_count = 0;
  unsigned long per_5s_last_time = 0;
  unsigned long per_1m_last_time = 0;
  NFCToken current;
  void serialHexdump(uint8_t *bytes, int len);
public:
  NFC(PN532_I2C &pn532i2c, PN532 &pn532, uint8_t reset_pin);
  nfctoken_callback_t token_present_callback = NULL;
  nfctoken_callback_t token_removed_callback = NULL;
  nfcreader_status_callback_t reader_status_callback = NULL;
  int pn532_check_interval = 10000;
  int pn532_reset_interval = 1000;
  int per_5s_limit = 30;
  int per_1m_limit = 60;
  bool read_counter = false;
  bool read_sig = false;
  int read_data = 0;
  int reset_count = 0;
  int token_count = 0;
  void begin();
  void loop();
  bool debug = false;
};

#endif

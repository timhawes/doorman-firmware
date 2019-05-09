#include "NFCReader.hpp"
#include <Arduino.h>

NFC::NFC(PN532_I2C &pn532i2c, PN532 &pn532, uint8_t reset_pin)
{
  _pn532i2c = &pn532i2c;
  _pn532 = &pn532;
  _reset_pin = reset_pin;
  ready = false;
}

void NFC::serialHexdump(uint8_t *bytes, int len)
{
  for (int i=0; i<len; i++) {
    char hex[4];
    sprintf(hex, "%02x ", bytes[i]);
    Serial.print(hex);
  }
  Serial.println();
}

void NFC::begin()
{
}

void NFC::loop()
{
  static boolean ready = false;
  static unsigned long lastTest = 0;
  static unsigned long lastReset = 0;
  static NFCToken last_token1;
  static NFCToken last_token2;

  // periodically check that the PN532 is responsive
  if (ready) {
    if (millis() > lastTest + pn532_check_interval) {
      uint32_t versiondata = _pn532->getFirmwareVersion();
      if (versiondata) {
        lastTest = millis();
      } else {
        Serial.println("PN532 is not responding");
        // unset the ready flag
        ready = false;
      }
    }
  }

  if (!ready) {
    if (millis() - lastReset > pn532_check_interval) {
      Serial.println("Resetting NFC");
      digitalWrite(_reset_pin, LOW);
      delay(10);
      digitalWrite(_reset_pin, HIGH);
      delay(10);
      lastReset = millis();
      reset_count++;
      _pn532->begin();
      uint32_t versiondata = _pn532->getFirmwareVersion();
      if (versiondata) {
        Serial.print("PN5");
        Serial.print((versiondata >> 24) & 0xFF, HEX);
        Serial.print(" ");
        Serial.print("V");
        Serial.print((versiondata >> 16) & 0xFF, DEC);
        Serial.print('.');
        Serial.print((versiondata >> 8) & 0xFF, DEC);
        ready = true;
        pn532_check_interval = pn532_check_interval_min;
      } else {
        pn532_check_interval *= 2;
        if (pn532_check_interval > pn532_check_interval_max) {
          pn532_check_interval = pn532_check_interval_max;
        }
        return;
      }
    } else {
      return;
    }

    // Set the max number of retry attempts to read from a token
    // This prevents us from waiting forever for a token, which is
    // the default behaviour of the PN532.
    _pn532->setPassiveActivationRetries(0x00);

    // Call setParameters function in the PN532 and disable the automatic ATS requests.
    //
    // Smart tokens would normally respond to ATS, causing the Arduino I2C buffer limit
    // to be reached and packets to be corrupted.
    /*
    uint8_t packet_buffer[64];
    packet_buffer[0] = 0x12;
    packet_buffer[1] = 0x24;
    _pn532i2c->writeCommand(packet_buffer, 2);
    _pn532i2c->readResponse(packet_buffer, sizeof(packet_buffer), 50);
    */

    // configure board to read RFID tags
    _pn532->SAMConfig();

    Serial.println(" ready");
    ready = true;
  }

  uint8_t packet_buffer[64];
  packet_buffer[0] = PN532_COMMAND_INLISTPASSIVETARGET;
  packet_buffer[1] = 1;
  packet_buffer[2] = PN532_MIFARE_ISO14443A;
  _pn532i2c->writeCommand(packet_buffer, 3);
  uint16_t response_len = _pn532i2c->readResponse(packet_buffer, sizeof(packet_buffer), 50);

  if (response_len > 0) {
    if (packet_buffer[0] > 0) {

      unsigned long read_start = millis();

      current.clear();
      current.atqa = (packet_buffer[2] << 8) | packet_buffer[3]; // sens_res
      current.sak = packet_buffer[4]; // sel_res
      uint8_t packet_uid_len = packet_buffer[5];
      current.setUid(&packet_buffer[6], packet_uid_len);
      if (response_len > 6 + packet_uid_len) {
        current.setAts(&packet_buffer[6+packet_uid_len], packet_buffer[6+packet_uid_len]);
      }

      if (last_token1.matchesUid(&packet_buffer[6], packet_uid_len)) {
        last_token1.setSeen();
      } else if (last_token2.matchesUid(&packet_buffer[6], packet_uid_len)) {
        last_token2.setSeen();
      } else {
        if (debug) {
          Serial.print("NFCReader: PN532_COMMAND_INLISTPASSIVETARGET -> ");
          serialHexdump(packet_buffer, response_len);
        }

        // read version data
        if (current.isIso14443dash4() || current.isNtag21x()) {
          uint8_t packet2[64];
          packet2[0] = PN532_COMMAND_INCOMMUNICATETHRU;
          packet2[1] = 0x60;
          _pn532i2c->writeCommand(packet2, 2);
          uint16_t packet2len = _pn532i2c->readResponse(packet2, sizeof(packet2), 50);
          if (packet2len > 0) {
            current.setVersion(&packet2[1], packet2len-1);
          }
        }

        // NTAG21x-specific data
        if (current.isNtag21x()) {
          uint8_t packet2[64];
          uint16_t packet2len;
          
          // read signature
          if (read_sig) {
            packet2[0] = PN532_COMMAND_INCOMMUNICATETHRU;
            packet2[1] = 0x3C;
            packet2[2] = 0x00;
            _pn532i2c->writeCommand(packet2, 3);
            packet2len = _pn532i2c->readResponse(packet2, sizeof(packet2), 50);
            if (packet2len == 33) {
              current.setNtagSignature(&packet2[1], 32);
            }
          }

          // read blocks
          if (read_data > 0) {
            int max_block = current.max_block;
            if (max_block > read_data) {
              // limit data read according to setting
              max_block = read_data;
            }
            for (int block=0; block<max_block; block+=12) {
              packet2[0] = PN532_COMMAND_INCOMMUNICATETHRU;
              packet2[1] = 0x3A;
              packet2[2] = block;
              packet2[3] = block+12;
              if (packet2[3] > max_block) {
                packet2[3] = max_block;
              }
              _pn532i2c->writeCommand(packet2, 4);
              packet2len = _pn532i2c->readResponse(packet2, sizeof(packet2), 64);
              current.setData(block*4, &packet2[1], packet2len-1);
            }
          }

          // read counter
          if (read_counter) {
            packet2[0] = PN532_COMMAND_INCOMMUNICATETHRU;
            packet2[1] = 0x39;
            packet2[2] = 0x02;
            _pn532i2c->writeCommand(packet2, 3);
            packet2len = _pn532i2c->readResponse(packet2, sizeof(packet2), 50);
            if (packet2len == 4) {
              current.ntag_counter = (packet2[3] << 16) | (packet2[2] << 8) | packet2[1];
            }
          }
        }

        current.read_time = millis() - read_start;

        if (debug) {
          current.dump();
        }

        if (last_token1.lastSeen() == -1) {
          last_token1.copyFrom(current);
          last_token1.setSeen();
        } else if (last_token2.lastSeen() == -1) {
          last_token2.copyFrom(current);
          last_token2.setSeen();
        }

        if (token_present_callback) {
          token_present_callback(current);
        }
      }
    }
  }

  if (last_token1.uid_len > 0 && last_token1.lastSeen() > token_present_timeout) {
    if (debug) {
      Serial.println("NFCReader: token #1 removed");
    }
    if (token_removed_callback) {
      token_removed_callback(last_token1);
    }
    last_token1.clear();
  }

  if (last_token1.uid_len > 0 && last_token2.lastSeen() > token_present_timeout) {
    if (debug) {
      Serial.println("NFCReader: token #2 removed");
    }
    if (token_removed_callback) {
      token_removed_callback(last_token2);
    }
    last_token2.clear();
  }

}

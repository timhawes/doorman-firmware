#include "FirmwareWriter.hpp"

#include <Updater.h>

FirmwareWriter::FirmwareWriter() {
  strncpy(_md5, "", sizeof(_md5));
  _size = 0;
}

void FirmwareWriter::Abort() {
  if (started) {
    // write some dummy data to break the MD5 check
    Update.write((uint8_t *)"_ABORT_", 7);
    // end the update
    Update.end();
    strncpy(_md5, "", sizeof(_md5));
    _size = 0;
    started = false;
  }
}

bool FirmwareWriter::Add(uint8_t *data, unsigned int len) {
  return Add(data, len, position);
}

bool FirmwareWriter::Add(uint8_t *data, unsigned int len, unsigned int pos) {
  if (pos != position) {
    Serial.print("FirmwareWriter: firmware position mismatch (expected=");
    Serial.print(position, DEC);
    Serial.print(" received=");
    Serial.println(pos, DEC);
    return false;
  }
  if (position == 0 && (!started)) {
    if (len < 4) {
      // need at least 4 bytes to check the file header
      Serial.println("FirmwareWriter: need at least 4 bytes to check the file header");
      return false;
    }
    if (data[0] != 0xE9) {
      // magic header doesn't start with 0xE9
      Serial.println("FirmwareWriter: magic header doesn't start with 0xE9");
      return false;
    }
    uint32_t bin_flash_size = ESP.magicFlashChipSize((data[3] & 0xf0) >> 4);
    // new file doesn't fit into flash
    if (bin_flash_size > ESP.getFlashChipRealSize()) {
      Serial.println("FirmwareWriter: new file won't fit into flash");
      return false;
    }
    Update.runAsync(true);
    if (!Update.begin(_size, U_FLASH)) {
      Serial.println("FirmwareWriter: Update.begin() failed");
      Update.printError(Serial);
      return false;
    }
    if (!Update.setMD5(_md5)) {
      Serial.println("FirmwareWriter: Update.setMD5() failed");
      Update.printError(Serial);
      return false;
    }
    started = true;
  }
  if (!started) {
    return false;
  }
  // Serial.print("FirmwareWriter: writing ");
  // Serial.print(len, DEC);
  // Serial.print(" bytes at position ");
  // Serial.println(position, DEC);
  if (Update.write(data, len) == len) {
    position += len;
    return true;
  } else {
    Update.printError(Serial);
    return false;
  }
}

bool FirmwareWriter::Begin(const char *md5, size_t size) {
  if (started) {
    return false;
  } else {
    _size = size;
    strncpy(_md5, md5, sizeof(_md5));
    position = 0;
    started = false;
    return true;
  }
}

bool FirmwareWriter::Commit() {
  if (started) {
    Serial.println("FirmwareWriter: finishing up");
    if (Update.end()) {
      Serial.println("FirmwareWriter: end() succeeded");
      return true;
    } else {
      Serial.println("FirmwareWriter: end() failed");
      Update.printError(Serial);
      Serial.println(Update.getError());
      return false;
    }
  } else {
    Serial.println("FirmwareWriter: nothing to commit");
    return false;
  }
}

int FirmwareWriter::GetUpdaterError() {
  return Update.getError();
}

bool FirmwareWriter::Open() {
  String current_md5 = ESP.getSketchMD5();
  if (strncmp(current_md5.c_str(), _md5, 32) == 0) {
    // this firmware is already installed
    Serial.println("FirmwareWriter: existing firmware has same md5");
    return false;
  }
  if (_size > (unsigned int)ESP.getFreeSketchSpace()) {
    // not enough space for new firmware
    Serial.println("FirmwareWriter: not enough free sketch space");
    return false;
  }
  position = 0;
  return true;
}

int FirmwareWriter::Progress() {
  if (_size > 0) {
    return (100 * position) / _size;
  } else {
    return 0;
  }
}

bool FirmwareWriter::Running() {
  return started;
}

bool FirmwareWriter::UpToDate() {
  String current_md5 = ESP.getSketchMD5();
  if (strncmp(current_md5.c_str(), _md5, 32) == 0) {
    return true;
  } else {
    return false;
  }
}

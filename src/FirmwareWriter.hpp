#ifndef FIRMWAREWRITER_HPP
#define FIRMWAREWRITER_HPP

#include <Arduino.h>

class FirmwareWriter {
 private:
  char _md5[33];
  size_t _size = 0;
  unsigned int position = 0;
  bool started = false;

 public:
  FirmwareWriter();
  void Abort();
  bool Add(uint8_t *data, unsigned int len, unsigned int pos);
  bool Add(uint8_t *data, unsigned int len);
  bool Begin(const char *md5, size_t size);
  bool Commit();
  int GetUpdaterError();
  bool Open();
  int Progress();
  bool Running();
  bool UpToDate();
};

#endif

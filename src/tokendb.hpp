#ifndef TOKENDB_HPP
#define TOKENDB_HPP

#include <Arduino.h>
#include <FS.h>

class TokenDB
{
private:
  char _filename[50];
  int access_level;
  String user;
  bool query_v1(File file, uint8_t uidlen, uint8_t *uid);
  bool query_v2(File file, uint8_t uidlen, uint8_t *uid);
public:
  TokenDB(const char *filename);
  bool lookup(uint8_t uidlen, uint8_t *uid);
  bool lookup(const char *uid);
  int get_access_level();
  String get_user();
};

#endif

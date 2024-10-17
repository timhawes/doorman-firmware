// SPDX-FileCopyrightText: 2018-2024 Tim Hawes
//
// SPDX-License-Identifier: MIT

#include "tokendb.hpp"
#include "app_util.h"
#ifdef ESP32
#include "MD5Builder.h"
#include "SPIFFS.h"
#endif

TokenDB::TokenDB(const char *filename)
{
  _filename = filename;
}

bool TokenDB::query_v1(File file, uint8_t uidlen, uint8_t *uid) {
  while (file.available()) {
    uint8_t xlen = file.read();
    uint8_t xuid[xlen];
    for (int i=0; i<xlen; i++) {
      xuid[i] = file.read();
    }
    if ((xlen == uidlen) && (memcmp(xuid, uid, xlen) == 0)) {
      Serial.println("TokenDB: v1 access-granted");
      file.close();
      access_level = 1;
      user = "unknown";
      return true;
    }
  }

  Serial.println("TokenDB: v1 not-found");
  file.close();
  return false;
}

bool TokenDB::query_v2(File file, uint8_t uidlen, uint8_t *uid) {
  int hash_bytes = file.read();
  int salt_length = file.read();
  char salt[salt_length+1];

  if (salt_length > 0) {
    for (int i=0; i<salt_length; i++) {
      salt[i] = file.read();
    }
    salt[salt_length] = 0;
  } else {
    salt[0] = 0;
  }

  uint8_t hash[8];

  MD5Builder md5;
  md5.begin();
  md5.add((uint8_t*)salt, salt_length);
  md5.add(uid, uidlen);
  md5.calculate();
  md5.getBytes(hash);

  while (file.available()) {
    uint8_t hashed_uid[hash_bytes];
    uint8_t access;
    uint8_t user_length;
    file.readBytes((char*)hashed_uid, hash_bytes);
    access = file.read();
    user_length = file.read();
    char new_user[user_length+1];
    file.readBytes(new_user, user_length);
    new_user[user_length] = 0;
    if (memcmp(hash, hashed_uid, hash_bytes) == 0) {
      access_level = access;
      user = new_user;
      if (access > 0) {
        Serial.println("TokenDB: v2 access>0");
        file.close();
        return true;
      } else {
        Serial.println("TokenDB: v2 access=0");
        file.close();
        return false;
      }
    }
  }
  Serial.println("TokenDB: v2 not-found");
  file.close();
  return false;
}

bool TokenDB::query_v3(File file, uint8_t uidlen, uint8_t *uid) {
  while (file.available()) {
    uint8_t xuidlen = file.read();
    uint8_t xuid[xuidlen];
    file.readBytes((char*)xuid, xuidlen);
    uint8_t user_length = file.read();
    char new_user[user_length+1];
    file.readBytes(new_user, user_length);
    new_user[user_length] = 0;
    if ((xuidlen == uidlen) && (memcmp(xuid, uid, xuidlen) == 0)) {
      Serial.println("TokenDB: v3 access-granted");
      file.close();
      access_level = 1;
      user = new_user;
      return true;
    }
  }

  Serial.println("TokenDB: v3 not-found");
  file.close();
  return false;
}

bool TokenDB::lookup(uint8_t uidlen, uint8_t *uidbytes)
{
  //Serial.print("looking for ");
  //Serial.print(uidlen, DEC);
  //Serial.println(" bytes UID in tokens files");

  //Serial.print(uidlen, HEX);
  //for (int i=0; i<uidlen; i++) {
  //  Serial.print(" ");
  //  Serial.print(uidbytes[i], HEX);
  //}
  //Serial.println();
  //Serial.println("---");

  access_level = 0;
  user = "";
  dbversion = -1;

  if (SPIFFS.exists(_filename)) {
    File tokens_file = SPIFFS.open(_filename, "r");
    if (tokens_file) {
      dbversion = tokens_file.read();
      switch (dbversion) {
        case 1:
          return query_v1(tokens_file, uidlen, uidbytes);
          break;
        case 2:
          return query_v2(tokens_file, uidlen, uidbytes);
          break;
        case 3:
          return query_v3(tokens_file, uidlen, uidbytes);
          break;
        default:
          Serial.print("TokenDB: unknown version ");
          Serial.println(dbversion, DEC);
          break;
      }
    } else {
      Serial.println("TokenDB: unable to open tokens file");
    }
  } else {
    Serial.println("TokenDB: tokens file not found");
  }

  return false;
}

bool TokenDB::lookup(const char *uid)
{
  uint8_t uidbytes[7];
  uint8_t uidlen;

  uidlen = decode_hex((const char*)uid, uidbytes, sizeof(uidbytes));

  return lookup(uidlen, uidbytes);
}

int TokenDB::get_access_level()
{
  return access_level;
}

String TokenDB::get_user()
{
  return user;
}

int TokenDB::get_version()
{
  return dbversion;
}

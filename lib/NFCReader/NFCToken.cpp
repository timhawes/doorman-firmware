#include "Arduino.h"
#include "NFCToken.hpp"

NFCToken::NFCToken()
{
  clear();
}

String NFCToken::hexlify(uint8_t bytes[], int len)
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

void NFCToken::clear()
{
  uid_len = 0;
  memset(&uid, 0, sizeof(uid));
  atqa = 0;
  sak = 0;
  ats_len = 0;
  memset(&ats, 0, sizeof(ats));
  version_len = 0;
  memset(&version, 0, sizeof(version));
  ntag_counter = 0;
  ntag_signature_len = 0;
  memset(&ntag_signature, 0, sizeof(ntag_signature));
  last_seen = 0;
  is_seen = false;
  max_block = 0;
  memset(&data, 0, sizeof(data));
  data_len = 0;
  read_time = 0;
}

void NFCToken::copyFrom(NFCToken source)
{
  clear();
  uid_len = source.uid_len;
  memcpy(&uid, source.uid, source.uid_len);
  atqa = source.atqa;
  sak = source.sak;
  ats_len = source.ats_len;
  memcpy(&ats, source.ats, source.ats_len);
  version_len = source.version_len;
  memcpy(&version, source.version, source.version_len);
  ntag_counter = source.ntag_counter;
  ntag_signature_len = source.ntag_signature_len;
  memcpy(&ntag_signature, source.ntag_signature, sizeof(ntag_signature));
}

void NFCToken::setUid(uint8_t *_uid, uint8_t _uid_len)
{
  uid_len = _uid_len;
  memset(&uid, 0, sizeof(uid));
  if (uid_len > MAX_UID_LENGTH) {
    uid_len = MAX_UID_LENGTH;
  }
  memcpy(&uid, _uid, uid_len);
}

void NFCToken::setAts(uint8_t *_ats, uint8_t _ats_len)
{
  ats_len = _ats_len;
  memset(&ats, 0, sizeof(ats));
  if (ats_len > MAX_ATS_LENGTH) {
    ats_len = MAX_ATS_LENGTH;
  }
  memcpy(&ats, _ats, ats_len);
}

void NFCToken::setVersion(uint8_t *_version, uint8_t _version_len)
{
  version_len = _version_len;
  memset(&version, 0, sizeof(version));
  if (version_len > sizeof(version)) {
    version_len = sizeof(version);
  }
  memcpy(&version, _version, version_len);

  if (memcmp(version, nfc_version_ntag213, 8) == 0) {
    max_block = 0x2C;
  } else if (memcmp(version, nfc_version_ntag215, 8) == 0) {
    max_block = 0x86;
  } else if (memcmp(version, nfc_version_ntag216, 8) == 0) {
    max_block = 0xE6;
  }
}

void NFCToken::setNtagSignature(uint8_t *sig, uint8_t siglen)
{
  memset(&ntag_signature, 0, sizeof(ntag_signature));
  if (siglen == sizeof(ntag_signature)) {
    memcpy(&ntag_signature, sig, sizeof(ntag_signature));
    ntag_signature_len = siglen;
  }
}

void NFCToken::setSeen()
{
  last_seen = millis();
  is_seen = true;
}

long NFCToken::lastSeen()
{
  if (is_seen) {
    return (long)(millis() - last_seen);
  } else {
    return -1;
  }
}

void NFCToken::dump()
{
  Serial.println("NFCToken:");
  char atqa_hex[5];
  char sak_hex[3];
  sprintf(atqa_hex, "%04x", atqa);
  sprintf(sak_hex, "%02x", sak);
  Serial.print("  atqa: "); Serial.println(atqa_hex);
  Serial.print("  sak: "); Serial.println(sak_hex);
  Serial.print("  uid: "); Serial.println(hexlify(uid, uid_len));
  Serial.print("  ats: "); Serial.println(hexlify(ats, ats_len));
  Serial.print("  version: "); Serial.println(hexlify(version, version_len));
  Serial.print("  ntag_counter: "); Serial.println(ntag_counter, DEC);
  Serial.print("  ntag_signature: "); Serial.println(hexlify(ntag_signature, ntag_signature_len));
  Serial.print("  data: "); Serial.println(hexlify(data, data_len));
  Serial.print("  read_time: "); Serial.println(read_time, DEC);
}

bool NFCToken::matchesUid(uint8_t *new_uid, uint8_t new_uid_len)
{
  if (new_uid_len > 0 && new_uid_len == uid_len) {
    if (memcmp(new_uid, uid, uid_len) == 0) {
      return true;
    }
  }

  return false;
}

bool NFCToken::isIso14443dash4()
{
  if ((sak & 0b00100100) == 0b00100000) {
    return true;
  } else {
    return false;
  }
}

bool NFCToken::isNtag21x()
{
  if (uid_len == 7 && atqa == 0x0044 && sak == 0x00) {
    return true;
  } else {
    return false;
  }
}

String NFCToken::uidString()
{
  return hexlify(uid, (int)uid_len);
}

void NFCToken::setData(int position, uint8_t *_data, uint8_t _datalen)
{
  if (position < 0 || position >= sizeof(data) || position + _datalen >= sizeof(data)) {
    return;
  }
  memcpy(&data[position], _data, _datalen);
  if (position + _datalen > data_len) {
    data_len = position + _datalen;
  }
}

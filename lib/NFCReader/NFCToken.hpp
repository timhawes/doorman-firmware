#ifndef NFCTOKEN_HPP
#define NFCTOKEN_HPP

#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>

#define MAX_UID_LENGTH 7
#define MAX_ATS_LENGTH 32

const uint8_t nfc_version_ntag213[] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x0F, 0x03};
const uint8_t nfc_version_ntag215[] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x11, 0x03};
const uint8_t nfc_version_ntag216[] = {0x00, 0x04, 0x04, 0x02, 0x01, 0x00, 0x13, 0x03};

class NFCToken {
private:
  bool is_seen = false;
  time_t last_seen;
  String hexlify(uint8_t *bytes, int len);
public:
  NFCToken();
  uint16_t atqa;
  uint8_t ats[MAX_ATS_LENGTH];
  uint8_t ats_len = 0;
  uint32_t ntag_counter;
  uint8_t ntag_signature[32];
  uint8_t ntag_signature_len = 0;
  uint8_t sak;
  uint8_t uid[MAX_UID_LENGTH];
  uint8_t uid_len = 0;
  uint8_t version[16];
  uint8_t version_len = 0;
  uint8_t max_block = 0;
  uint8_t data[1024];
  uint16_t data_len = 0;
  unsigned int read_time = 0;
  void clear();
  void copyFrom(NFCToken source);
  void dump();
  bool isIso14443dash4();
  bool isNtag21x();
  long lastSeen();
  bool matchesUid(uint8_t *uid, uint8_t uidlen);
  void setAts(uint8_t *ats, uint8_t atslen);
  void setSeen();
  void setNtagSignature(uint8_t *sig, uint8_t siglen);
  void setUid(uint8_t *uid, uint8_t uidlen);
  void setVersion(uint8_t *version, uint8_t versionlen);
  void setData(int position, uint8_t *data, uint8_t datalen);
  String uidString();
};

#endif

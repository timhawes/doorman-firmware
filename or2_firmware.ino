/*
 * Hardware:
 * ESP-12E
 * Elechouse PN532
 *
 * Libraries:
 * https://github.com/Seeed-Studio/PN532
 *
 */

#include <Wire.h>
#include <ESP8266WiFi.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <Ticker.h>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include "config.h"

// #define DEBUG

#ifdef DEBUG
 #define DEBUG_PRINT(x) Serial.print(x)
 #define DEBUG_PRINTDEC(x) Serial.print(x, DEC)
 #define DEBUG_PRINTHEX(x) Serial.print(x, HEX)
 #define DEBUG_PRINTLN(x) Serial.println(x)
#else
 #define DEBUG_PRINT(x)
 #define DEBUG_PRINTDEC(x)
 #define DEBUG_PRINTHEX(x)
 #define DEBUG_PRINTLN(x)
#endif

#define PP_HTONS(x) ((((x) & 0xff) << 8) | (((x) & 0xff00) >> 8))
#define PP_NTOHS(x) PP_HTONS(x)
#define PP_HTONL(x) ((((x) & 0xff) << 24) | \
                     (((x) & 0xff00) << 8) | \
                     (((x) & 0xff0000UL) >> 8) | \
                     (((x) & 0xff000000UL) >> 24))
#define PP_NTOHL(x) PP_HTONL(x)

#define S_TO_MS(x) (x*1000)
#define DS_TO_MS(x) (x*100)
#define CS_TO_MS(x) (x*10)

#define DOOR_CLOSED 0
#define DOOR_OPEN 1

#define POWER_MAINS 0
#define POWER_BATTERY 1

#define LED_OFF 0
#define LED_DIM 1
#define LED_ON 2
#define LED_BEACON 3
#define LED_FAST 4
#define LED_MEDIUM 5
#define LED_SLOW 6

const uint8_t exitPin = 4;
const uint8_t buzzerPin = 0;
const uint8_t doorPin = 14;
const uint8_t sdaPin = 13;
const uint8_t sclPin = 12;
const uint8_t relayPin = 15;
const uint8_t ledPin = 16;
const uint8_t snibPin = 5;
const uint8_t pnResetPin = 2;

const int remote_udp_port = 21046;
const int local_udp_port = 21045;

Ticker fastLoopTicker;

#define MAX_AUTHORIZED_CARDS 508
#define MAX_UID_LENGTH 7
#define EEPROM_CARD_DATABASE_LOCATION 32

struct AuthorizedCard {
  uint8_t uidlen;
  uint8_t uid[MAX_UID_LENGTH];
};
struct AuthorizedCard cardDatabase[MAX_AUTHORIZED_CARDS];

struct Settings {
  uint16_t snibUnlockTime            = 3600; // seconds
  uint8_t  exitUnlockMinTime         = 25;   // cs
  uint8_t  exitUnlockMaxTime         = 10;   // seconds
  uint8_t  cardUnlockTime            = 5;    // seconds
  uint8_t  pn532CheckInterval        = 30;   // seconds
  uint8_t  authNetworkResendInterval = 150;  // ms
  uint8_t  authNetworkTimeout        = 5;    // ds
  uint16_t voltageScaleMultiplier    = 500;
  uint16_t voltageScaleDivider       = 340;
  uint16_t voltageRisingThreshold    = 1370; // centivolts
  uint16_t voltageFallingThreshold   = 1360; // centivolts
  uint8_t  voltageCheckInterval      = 5;    // seconds
  uint8_t  cardPresentTimeout        = 35;   // cs
  uint8_t  longPressTime             = 10;   // ds
  uint8_t  systemInfoInterval        = 5;    // seconds
  uint8_t  statusMinInterval         = 1;    // ds
  uint8_t  statusMaxInterval         = 50;   // ds
  uint8_t  doorAlarmTime             = 0;    // seconds
  uint8_t  errorSoundsEnabled        = 0;
  uint8_t  allowSnibOnBattery        = 0;
  uint8_t  helloFastInterval         = 100;  // cs
  uint8_t  helloSlowInterval         = 10;   // s
  uint8_t  helloResponseTimeout      = 21;   // s
} settings;

boolean sendStatusPacket = true;
boolean sendSystemInfoPacket = true;
unsigned int statusInterval;
unsigned long statusLastSent;
unsigned long systemInfoLastSent;
unsigned long helloLastSent;
char clientid[24];

struct AuthRequestPacket {
  uint8_t cmd = 0x03;
  uint8_t uidLen = 0;
  uint8_t uid[7];
} authRequestPacket;
unsigned long authRequestLastSent;

uint8_t authState = 0;
uint8_t authUidLen = 0;
uint8_t authUid[7];
unsigned long authStarted = 0;

boolean wifiConnected = 0;
boolean clientConnected = 0;

boolean cardDatabaseSendRequested = false;
uint16_t cardDatabaseSendPosition = 0x0000;
uint16_t cardDatabaseSendFinish = 0x0000;

unsigned long lastHelloResponse;

volatile boolean cardUnlockActive = false;
volatile unsigned long cardUnlockUntil = 0;

volatile boolean exitUnlockActive = false;
volatile unsigned long exitUnlockUntil = 0;

volatile boolean snibUnlockActive = false;
volatile unsigned long snibUnlockUntil = 0;

uint16_t secondsOffline = 0;
uint16_t pn532ResetCount = 0;
uint16_t wifiConnectionCount = 0;
uint16_t clientConnectionCount = 0;
uint8_t doorState = 0;
uint8_t exitButtonState;
uint8_t snibButtonState;
uint8_t powerMode;
uint16_t batteryVoltage;
uint16_t batteryAdc;
uint8_t doorForced;
uint8_t doorAjar;
uint8_t exitEnabled = true;
uint8_t snibEnabled = true;

unsigned long eepromLastPendingChange;
boolean eepromChangesPending;

uint8_t ledMode = LED_BEACON;

uint16_t soundHighChirp[] = {1000, 100, 0, 0};
uint16_t soundMidChirp[] = {500, 100, 0, 0};
uint16_t soundLowChirp[] = {256, 100, 0, 0};
uint16_t soundHigh[] = {1000, 500, 0, 0};
uint16_t soundMid[] = {512, 500, 0, 0};
uint16_t soundLow[] = {256, 500, 0, 0};
uint16_t soundTwoTone[] = {256, 100, 1000, 100, 0, 0};
uint16_t soundBeeb[] = {256, 375, 0, 50, 500, 300, 0, 0};
uint16_t soundTinyChirp[] = {500, 50, 0, 0};
uint16_t soundMicroChirp[] = {500, 25, 0, 0};
uint16_t soundTriPulse[] = {256, 25, 0, 25, 256, 25, 0, 25, 256, 25, 0, 0};

#define MAX_CUSTOM_SOUND_EVENTS 512
uint16_t soundCustom[MAX_CUSTOM_SOUND_EVENTS*2] = {};

uint16_t *soundPattern;
uint16_t soundPosition = 0;
boolean soundActive = false;

WiFiUDP Udp;

PN532_I2C pn532i2c(Wire);
PN532 nfc(pn532i2c);

void setup(void) {

  Serial.begin(115200);
  Serial.println();
  Serial.println("Hello!");

  pinMode(exitPin, INPUT_PULLUP);
  pinMode(doorPin, INPUT_PULLUP);
  pinMode(snibPin, INPUT_PULLUP);
  pinMode(relayPin, OUTPUT); digitalWrite(relayPin, LOW);
  pinMode(buzzerPin, INPUT);
  pinMode(ledPin, OUTPUT); digitalWrite(ledPin, LOW);
  pinMode(pnResetPin, OUTPUT); digitalWrite(pnResetPin, HIGH);

  // the watchdog will sometimes cause a hangup while attempting to reboot
  // give it a large timeout so we don't go for a reboot too soon
  ESP.wdtEnable(30000);

  EEPROM.begin(4096);

  uint16_t test;
  EEPROM.get(0, test);
  if (test != 0xFFFF) {
    // load from EEPROM, but only if it is not blank
    EEPROM.get(0, settings);
    EEPROM.get(EEPROM_CARD_DATABASE_LOCATION, cardDatabase);
  }
  eepromChangesPending = false;

  dumpSystemInfo();

  // prepare an identity string for Hello packets
  snprintf(clientid, sizeof(clientid), "ESP_OR_%08X", ESP.getChipId());

  Wire.begin(sdaPin, sclPin);
  WiFi.begin(ssid, wpa_password);

  // startup sound
  // we run this even though it probably won't be noticed,
  // because the first sound doesn't always get to the speaker
  soundPattern = soundMicroChirp;
  soundPosition = 0;
  soundActive = true;

  // initialise the interrupt loop
  fastLoopTicker.attach(0.01, fastLoop);
}

void loop()
{
  static unsigned long lastDiag = 0;

  nfcLoop();
  voltageLoop();
  wifiLoop();
  errorChirpLoop();

  if (doorForced || cardUnlockActive || exitUnlockActive) {
    ledMode = LED_FAST;
  } else if (snibUnlockActive) {
    ledMode = LED_MEDIUM;
  } else if (powerMode==POWER_BATTERY) {
    ledMode = LED_DIM;
  } else if (wifiConnected==false || clientConnected==false) {
    ledMode = LED_BEACON;
  } else {
    ledMode = LED_ON;
  }

  // commit EEPROM data 10 seconds after the last change
  if (eepromChangesPending) {
    if (millis()-eepromLastPendingChange > 10000) {
      commitEeprom();
    }
  }
}

void dumpSystemInfo()
{
  uint32_t chipId = ESP.getChipId();
  uint32_t flashChipId = ESP.getFlashChipId();
  uint32_t flashChipSize = ESP.getFlashChipSize();
  uint32_t flashChipSpeed = ESP.getFlashChipSpeed();

  Serial.print("ChipId="); Serial.print(chipId, HEX);
  Serial.print(" FlashChipId="); Serial.print(flashChipId, HEX);
  Serial.print(" FlashChipSize="); Serial.print(flashChipSize, DEC);
  Serial.print(" FlashChipSpeed="); Serial.print(flashChipSpeed, DEC);
  Serial.print(" FreeHeap="); Serial.print(ESP.getFreeHeap(), DEC);
  Serial.println();
}

void wifiLoop()
{
  static boolean oldClientConnected = false;
  static int oldState = 0;
  static unsigned long lastClientDisconnect = 0;
  int newState;

  newState = WiFi.status();
  if (newState != oldState) {
    Serial.print("WiFi changed state ");
    Serial.print(oldState, DEC);
    Serial.print(" -> ");
    Serial.println(newState, DEC);
    oldState = newState;
  }

  if (newState == WL_CONNECTED) {
    if (wifiConnected == false) {
      wifiConnected = true;
      wifiConnectionCount++;
      onWifiConnect();
    }
    udpClientLoop();
  } else {
    if (wifiConnected == true) {
      wifiConnected = false;
      onWifiDisconnect();
    }
    clientConnected = false;
  }

  if (oldClientConnected == false && clientConnected == true) {
    oldClientConnected = true;
    secondsOffline = secondsOffline + ((millis()-lastClientDisconnect)/1000);
    clientConnectionCount++;
    onClientConnect();
  } else if (oldClientConnected == true && clientConnected == false) {
    oldClientConnected = false;
    lastClientDisconnect = millis();
    onClientDisconnect();
  }
}

void errorChirpLoop()
{
  static unsigned long lastChirp = 0;
  static boolean ready = false;

  if (!settings.errorSoundsEnabled) {
    return;
  }

  if (!ready) {
    // don't chirp immediately, wait one interval first
    lastChirp = millis();
    ready = true;
    return;
  }

  if (wifiConnected==false || clientConnected==false || powerMode==POWER_BATTERY) {
    if (millis() > lastChirp + 10000) {
      if (soundActive == false) {
        soundPattern = soundTriPulse;
        soundPosition = 0;
        soundActive = true;
        lastChirp = millis();
      }
    }
  }
}

void voltageLoop()
{
  static unsigned long lastRead = 0;
  static unsigned int lastVoltage = 0;

  if (millis() > lastRead + S_TO_MS(settings.voltageCheckInterval)) {
    lastRead = millis();
    int adcValue = 0;
    for (int i = 0; i < 10; i++) {
      adcValue += analogRead(A0);
    }
    adcValue = adcValue / 10;
    unsigned int voltage = adcValue * settings.voltageScaleMultiplier / settings.voltageScaleDivider;
    batteryAdc = adcValue;
    batteryVoltage = voltage;
    //Serial.print("Voltage ");
    //Serial.print(voltage, DEC);
    //Serial.print("cV ");
    //Serial.println(adcValue, DEC);
    if (voltage != lastVoltage) {
      lastVoltage = voltage;
      // status packet is not pushed at this point, let the status interval decide when to send
    }
    if ((powerMode==POWER_BATTERY) && (voltage >= settings.voltageRisingThreshold)) {
      powerMode = POWER_MAINS;
      onMains();
    } else if ((powerMode==POWER_MAINS) && (voltage <= settings.voltageFallingThreshold)) {
      powerMode = POWER_BATTERY;
      onBattery();
    }
  }
}


void nfcLoop()
{
  static boolean ready = false;
  static unsigned long lastTest = 0;
  static uint8_t oldUidLength = 0;
  static uint8_t oldUid[MAX_UID_LENGTH];
  static unsigned long lastCard = 0;
  uint8_t newUidLength = 0;
  uint8_t newUid[MAX_UID_LENGTH];
  boolean success;

  memset(&oldUid[0], 0, sizeof(oldUid));

  if (!ready) {
    uint32_t versiondata = 0;
    digitalWrite(pnResetPin, LOW);
    delay(50);
    digitalWrite(pnResetPin, HIGH);
    delay(100);
    pn532ResetCount++;
    nfc.begin();
    versiondata = nfc.getFirmwareVersion();

    if (versiondata) {
      Serial.print("PN5");
      Serial.print((versiondata >> 24) & 0xFF, HEX);
      Serial.print(" ");
      Serial.print("V");
      Serial.print((versiondata >> 16) & 0xFF, DEC);
      Serial.print('.');
      Serial.print((versiondata >> 8) & 0xFF, DEC);
    } else {
      return;
    }

    // Set the max number of retry attempts to read from a card
    // This prevents us from waiting forever for a card, which is
    // the default behaviour of the PN532.
    nfc.setPassiveActivationRetries(0x00);

    // Call setParameters function in the PN532 and disable the automatic ATS requests.
    //
    // Smart cards would normally respond to ATS, causing the Arduino I2C buffer limit
    // to be reached and packets to be corrupted.
    uint8_t packet_buffer[64];
    packet_buffer[0] = 0x12;
    packet_buffer[1] = 0x24;
    pn532i2c.writeCommand(packet_buffer, 2);
    pn532i2c.readResponse(packet_buffer, sizeof(packet_buffer), 50);

    // configure board to read RFID tags
    nfc.SAMConfig();

    Serial.println(" ready");
    ready = true;
    lastTest = millis();
  }

  // periodically check that the PN532 is responsive
  if (millis() > lastTest + S_TO_MS(settings.pn532CheckInterval)) {
    uint32_t versiondata = nfc.getFirmwareVersion();
    if (versiondata) {
      lastTest = millis();
    } else {
      Serial.println("PN532 is not responding");
      // unset the ready flag so the PN532 will be reinitalised on the next loop
      ready = false;
      return;
    }
  }

  // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
  // 'uid' will be populated with the UID, and uidLength will indicate
  // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &newUid[0], &newUidLength, 100);

  if (success) {
    lastCard = millis();
    if ((oldUidLength != newUidLength) && (memcmp(oldUid, newUid, newUidLength) != 0)) {
      //Serial.println("new card");
      onCard(newUidLength, newUid);
      oldUidLength = newUidLength;
      memcpy(oldUid, newUid, sizeof(oldUid));
      authRequestPacket.uidLen = newUidLength;
      authUidLen = newUidLength;
      memcpy(authRequestPacket.uid, newUid, sizeof(authRequestPacket.uid));
      memcpy(authUid, newUid, sizeof(authUid));
      sendStatusPacket = true;
    }
  } else {
    if (oldUidLength != 0) {
      if (millis() - lastCard > CS_TO_MS(settings.cardPresentTimeout)) {
        //Serial.println("card removed");
        onCardRemoved();
        oldUidLength = 0;
        memset(&oldUid[0], 0, sizeof(oldUid));
        memset(authRequestPacket.uid, 0, sizeof(authRequestPacket.uid));
        memset(authUid, 0, sizeof(authUid));
        authRequestPacket.uidLen = 0;
        authUidLen = 0;
        sendStatusPacket = true;
      }
    }
  }
}

void udpClientLoop()
{
  static boolean ready = false;
  static unsigned long lastAttempt = 0;
  static uint16_t cardDatabaseIndex = 0;

  if (clientConnected == true && (millis() - lastHelloResponse > S_TO_MS(settings.helloResponseTimeout))) {
    Serial.println("Timeout, no hello response received");
    clientConnected = false;
  }

  if (!ready) {
    Udp.begin(local_udp_port);
    ready = true;
  }

  udpReceiveMessages();

  if (clientConnected == false && (millis() - helloLastSent > CS_TO_MS(settings.helloFastInterval))) {
    //Serial.println("Sending hello (fast interval)");
    Udp.begin(local_udp_port);
    Udp.beginPacketMulticast(IPAddress(255, 255, 255, 255), remote_udp_port, WiFi.localIP());
    Udp.write((uint8_t)0x00);
    Udp.write(clientid, strlen(clientid));
    Udp.write((uint8_t)0x00);
    Udp.endPacket();
    helloLastSent = millis();
  }

  if (authState == 1) {
    if (millis() - authRequestLastSent > settings.authNetworkResendInterval) {
      udpReceiveMessages();
      Udp.beginPacketMulticast(IPAddress(255, 255, 255, 255), remote_udp_port, WiFi.localIP());
      Udp.write((const uint8_t*)&authRequestPacket, sizeof(authRequestPacket));
      Udp.endPacket();
      authRequestLastSent = millis();
    }
  } else {
    authRequestLastSent = millis();
  }

  if (cardDatabaseSendRequested && cardDatabaseSendPosition<=cardDatabaseSendFinish) {
    udpReceiveMessages();
    //Serial.println("send starting");
    Udp.beginPacketMulticast(IPAddress(255, 255, 255, 255), remote_udp_port, WiFi.localIP());
    Udp.write(0x04);
    uint8_t total = 0;
    while (total<48 && cardDatabaseSendPosition<=cardDatabaseSendFinish) {
        //Serial.print("Sending slot "); Serial.println(cardDatabaseSendPosition, DEC);
        Udp.write(cardDatabaseSendPosition >> 8);
        Udp.write(cardDatabaseSendPosition & 0xFF);
        if (cardDatabase[cardDatabaseSendPosition].uidlen==0xFF) {
          // if uidlen is 0xFF (default for blank flash)
          // then pretend it is zero instead
          // this will reduce our sync transactions
          Udp.write((uint8_t)0x00);
          Udp.write((uint8_t)0x00);
          Udp.write((uint8_t)0x00);
          Udp.write((uint8_t)0x00);
          Udp.write((uint8_t)0x00);
          Udp.write((uint8_t)0x00);
          Udp.write((uint8_t)0x00);
          Udp.write((uint8_t)0x00);
        } else {
          Udp.write(cardDatabase[cardDatabaseSendPosition].uidlen);
          Udp.write((const uint8_t*)&cardDatabase[cardDatabaseSendPosition].uid, sizeof(cardDatabase[cardDatabaseSendPosition].uid));
        }
        total++;
        cardDatabaseSendPosition++;
    }
    Udp.endPacket();
    //Serial.println("send stopped");
    if (cardDatabaseSendPosition > cardDatabaseSendFinish) {
      cardDatabaseSendRequested = false;
      //cardDatabaseSendStart = 0;
      //Serial.println("send completed");
    }
  }

  if (clientConnected == true) {
    // only send status and systeminfo packets when the connection is known to be up

    if (sendStatusPacket) {
      // reset the status interval if there was a change
      statusInterval = DS_TO_MS(settings.statusMinInterval);
    }

    if (sendStatusPacket || (millis() - statusLastSent > statusInterval)) {

      udpReceiveMessages();

      //Serial.println("sending status");
      Udp.beginPacketMulticast(IPAddress(255, 255, 255, 255), remote_udp_port, WiFi.localIP());
      Udp.write(0x05);
      sendStatusPacket = false;
      sendFastStatus();
      if (millis() - systemInfoLastSent > S_TO_MS(settings.systemInfoInterval)) {
        //Serial.println("sending system info");
        sendSlowStatus();
        systemInfoLastSent = millis();
      }
      Udp.endPacket();
      statusLastSent = millis();
      statusInterval *= 2;
      if (statusInterval > DS_TO_MS(settings.statusMaxInterval)) {
        statusInterval = DS_TO_MS(settings.statusMaxInterval);
      }

    }

  }

  if (millis() - helloLastSent > S_TO_MS(settings.helloSlowInterval)) {
    udpReceiveMessages();
    //Serial.println("Sending hello (slow interval)");
    Udp.beginPacketMulticast(IPAddress(255, 255, 255, 255), remote_udp_port, WiFi.localIP());
    Udp.write((uint8_t)0x00);
    Udp.write(clientid, strlen(clientid));
    Udp.write((uint8_t)0x00);
    Udp.endPacket();
    helloLastSent = millis();
  }

}

void udpReceiveMessages()
{
  int inboundMessageLength = 0;
  uint8_t inboundMessage[1500];
  unsigned int packetSize;
  IPAddress remoteIp;
  int remotePort;
  int len;

  while (1) {
    packetSize = Udp.parsePacket();
    if (packetSize > 0) {
      remoteIp = Udp.remoteIP();
      remotePort = Udp.remotePort();

      Serial.print("Packet received size=");
      Serial.print(packetSize);
      Serial.print(" from=");
      Serial.print(remoteIp);
      Serial.print(":");
      Serial.print(remotePort);

      len = Udp.read(inboundMessage, sizeof(inboundMessage));
      if (len > 0) {
        Serial.print(" data=");
        for (int i = 0; i < len; i++) {
          if (inboundMessage[i] < 16) {
            Serial.print('0');
          }
          Serial.print(inboundMessage[i], HEX);
          Serial.print(' ');
        }
      }
      Serial.println();
      if (len > 0) {
        onMessageReceived(len, inboundMessage);
      }
    } else {
      break;
    }
  }
}

void onWifiConnect()
{
  Serial.print("Wifi connected ");
  Serial.println(WiFi.localIP());
}

void onWifiDisconnect()
{
  Serial.println("Wifi disconnected");
}

void onClientConnect()
{
  Serial.println("Client connected");
}

void onClientDisconnect()
{
  Serial.println("Client disconnected");
}

void onMessageReceived(int len, uint8_t message[])
{
  switch (message[0]) {

    case 0x80:
      lastHelloResponse = millis();
      clientConnected = true;
      break;

    //case 0x85:
    //  ledMode = message[1];
    //  break;

    case 0x86: {
      noInterrupts();
      switch (message[1]) {
        case 0:
          pinMode(buzzerPin, INPUT);
          break;
        case 1:
          pinMode(buzzerPin, OUTPUT);
          digitalWrite(buzzerPin, HIGH);
          break;
        case 2:
          pinMode(buzzerPin, OUTPUT);
          digitalWrite(buzzerPin, LOW);
          break;
      }
      switch (message[2]) {
        case 0:
          ESP.reset();
          break;
        case 1:
          ESP.restart();
          break;
      }
      break;
    }

    case 0x89:
      if (message[1] == 0) {
        exitEnabled = false;
        exitUnlockActive = false;
      } else {
        exitEnabled = true;
      }
      break;

    case 0x90:
      switch (message[1]) {
        case 1:
          // disable snib
          snibEnabled = false;
          break;
        case 2:
          // enable snib
          snibEnabled = true;
          break;
      }
      switch (message[2]) {
        case 1:
          // deactivate snib
          snibUnlockActive = false;
          break;
        case 2:
          // activate snib
          snibUnlockActive = true;
          snibUnlockUntil = millis() + S_TO_MS(settings.snibUnlockTime);
          break;
        case 3:
          // renew snib timeout
          if (snibUnlockActive) {
            snibUnlockUntil = millis() + S_TO_MS(settings.snibUnlockTime);
          }
          break;
      }
      break;

    case 0x91:
      onNetworkAuthResponse(message[1]);
      break;

    case 0x92:
      // dump card database to serial console
      dumpAuthorizationDatabaseToSerial();
      break;

    // set uid for a given card database slot
    // byte 01..02 = slot
    // byte 03 = uid length
    // byte 04..0A = uid
    case 0x93: {
      int pos = 1;
      uint16_t slot;
      uint8_t uidlen;
      while (pos<len) {
        //Serial.print("pos ");
        //Serial.println(pos, DEC);
        if (len-pos >= 3) {
          slot = (message[pos] << 8) + message[pos+1];
          uidlen = message[pos+2];
          //Serial.print("slot ");
          //Serial.println(slot, DEC);
          //Serial.print("uidlen ");
          //Serial.println(uidlen, DEC);
          pos += 3;
          if (uidlen==0) {
            cardDatabase[slot].uidlen = 0;
            Serial.print("setting slot ");
            Serial.println(slot, DEC);
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
          } else if (len-pos >= uidlen) {
            cardDatabase[slot].uidlen = uidlen;
            for (int i=0; i<uidlen; i++) {
              cardDatabase[slot].uid[i] = message[pos+i];
            }
            pos += uidlen;
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
          } else {
            Serial.println("out of data (uid)");
            break;
          }
        } else {
          Serial.println("out of data (slot)");
          break;
        }
      }
      //Serial.println("fin");
      break;
    }

    // commit settings and card authorization database to eeprom
    case 0x94:
      commitEeprom();
      break;

    // send selected card database entries to network
    // <start:u16> <finish:u16>
    case 0x95: {
        uint16_t start = (message[1] << 8) + message[2];
        uint16_t finish = (message[3] << 8) + message[4];
        if (start < 0) {
          start = 0;
        }
        if (finish >= MAX_AUTHORIZED_CARDS) {
          finish = MAX_AUTHORIZED_CARDS-1;
        }
        Serial.print("Card entries ");
        Serial.print(start);
        Serial.print(" to ");
        Serial.print(finish);
        Serial.println(" requested");
        cardDatabaseSendPosition = start;
        cardDatabaseSendFinish = finish;
        cardDatabaseSendRequested = true;
        break;
      }

    // play a custom sound sequence
    // <freq:u16> <millis:u16> [<freq:u16> <millis:u16> ...]
    case 0x96: {
      int pktPos = 1;
      int sndPos = 0;
      //Serial.print("custom sound: ");
      memset(soundCustom, 0, sizeof(soundCustom));
      while (pktPos<len && sndPos<MAX_CUSTOM_SOUND_EVENTS) {
        soundCustom[sndPos] = (message[pktPos] << 8) + message[pktPos+1];
        //Serial.print(soundCustom[sndPos]);
        //Serial.print(" ");
        pktPos += 2;
        sndPos++;
      }
      //Serial.println();
      soundPattern = soundCustom;
      soundPosition = 0;
      soundActive = true;
      break;
    }

    // set a variable
    // <varcode:u8> <data...> [<varcode:u8> <data...> ...]
    case 0x97: {
      int pos = 1;
      while (pos+1<len) {
        switch (message[pos]) {
          case 0x41:
            onNetworkAuthResponse(message[pos+1]);
            pos += 2;
            break;
          case 0x42:
            snibEnabled = message[pos+1];
            pos += 2;
            break;
          case 0x43:
            exitEnabled = message[pos+1];
            pos += 2;
            break;
          case 0x44:
            doorForced = message[pos+1];
            pos += 2;
            break;
          case 0x61:
            settings.cardUnlockTime = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x62:
            settings.exitUnlockMinTime = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x63:
            settings.exitUnlockMaxTime = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x64:
            settings.snibUnlockTime = (message[pos+1] << 8) + message[pos+2];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 3;
            break;
          case 0x65:
            settings.pn532CheckInterval = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x66:
            settings.authNetworkResendInterval = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x67:
            settings.authNetworkTimeout = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x68:
            settings.voltageScaleMultiplier = (message[pos+1] << 8) + message[pos+2];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 3;
            break;
          case 0x69:
            settings.voltageScaleDivider = (message[pos+1] << 8) + message[pos+2];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 3;
            break;
          case 0x6A:
            settings.voltageRisingThreshold = (message[pos+1] << 8) + message[pos+2];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 3;
            break;
          case 0x6B:
            settings.voltageFallingThreshold = (message[pos+1] << 8) + message[pos+2];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 3;
            break;
          case 0x6C:
            settings.voltageCheckInterval = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x6D:
            settings.cardPresentTimeout = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x6E:
            settings.longPressTime = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x6F:
            settings.systemInfoInterval = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x70:
            settings.statusMinInterval = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x71:
            settings.statusMaxInterval = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x72:
            settings.doorAlarmTime = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x73:
            settings.errorSoundsEnabled = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x74:
            settings.allowSnibOnBattery = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x75:
            settings.helloFastInterval = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x76:
            settings.helloSlowInterval = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          case 0x77:
            settings.helloResponseTimeout = message[pos+1];
            eepromChangesPending = true;
            eepromLastPendingChange = millis();
            pos += 2;
            break;
          default:
            Serial.print("unknown variable code, aborting");
            Serial.println(message[pos], DEC);
            break;
        }
      }
      break;
    }
    
    // trigger a card-unlock by proxy
    case 0x98: {
      onNetworkProxyAuth();
      break;
    }
    
  }
}

void dumpAuthorizationDatabaseToSerial()
{
  for (int i = 0; i < MAX_AUTHORIZED_CARDS; i++) {
    uint8_t len = cardDatabase[i].uidlen;
    if (len > 7) len = 0;
    if (len > 0) {
      Serial.print(i, DEC);
      Serial.print(" ");
      Serial.print(len);
      Serial.print(" ");
      for (uint8_t j = 0; j < len; j++) {
        if (cardDatabase[i].uid[j] < 16) {
          Serial.print("0");
        }
        Serial.print(cardDatabase[i].uid[j], HEX);
      }
      Serial.println();
    }
  }
}

void exitButtonUp()
{
  Serial.println("exitButtonUp");
  exitButtonState = 0;
  sendStatusPacket = true;
  if (exitUnlockActive) {
    // already unlocked, so just reduce the timeperiod back to minimum
    exitUnlockUntil = millis() + CS_TO_MS(settings.exitUnlockMinTime);
  }
}

void exitButtonDown()
{
  Serial.println("exitButtonDown");
  exitButtonState = 1;
  sendStatusPacket = true;
  if (exitEnabled) {
    exitUnlockActive = true;
    exitUnlockUntil = millis() + S_TO_MS(settings.exitUnlockMaxTime);
  }
}

void exitLongPress()
{
  Serial.println("exitLongPress");
  exitButtonState = 2;
  sendStatusPacket = true;
}

void doorOpen()
{
  Serial.println("doorOpen");
  doorState = DOOR_OPEN;
  sendStatusPacket = true;
}

void doorClosed()
{
  Serial.println("doorClosed");
  doorState = DOOR_CLOSED;
  sendStatusPacket = true;
}

void snibButtonUp()
{
  Serial.println("snibButtonUp");
  snibButtonState = 0;
  sendStatusPacket = true;
}

void snibButtonDown()
{
  Serial.println("snibButtonDown");
  snibButtonState = 1;
  sendStatusPacket = true;
  if (snibUnlockActive) {
    // already unlocked, so flip back to locked
    snibUnlockActive = false;
  } else if (snibEnabled && (powerMode==0 || settings.allowSnibOnBattery)) {
    snibUnlockActive = true;
    snibUnlockUntil = millis() + S_TO_MS(settings.snibUnlockTime);
  }
}

void snibLongPress()
{
  Serial.println("snibLongPress");
  snibButtonState = 2;
  sendStatusPacket = true;

  // use longpress to silence the current sound pattern
  soundActive = false;
}

void onNetworkProxyAuth()
{
  Serial.println("Accepting authentication by proxy");
  soundPattern = soundHighChirp;
  soundPosition = 0;
  soundActive = true;
  cardUnlockActive = true;
  cardUnlockUntil = millis() + S_TO_MS(settings.cardUnlockTime);
  authState = 6;
  sendStatusPacket = true;  
}

void onNetworkAuthResponse(uint8_t response)
{
  unsigned long now = millis();

  if (authState == 0) {
    Serial.println("Received authentication response but no request is pending");
    return;
  }
  if (authState >= 2) {
    Serial.print("Received authentication response but request has already been processed (time=");
    Serial.print(now - authStarted, DEC);
    Serial.println(")");
    return;
  }

  Serial.print("Accepting authentication response ");
  Serial.print(response, DEC);
  Serial.print(" (time=");
  Serial.print(now - authStarted, DEC);
  Serial.println(")");
  authState = response;
  sendStatusPacket = true;

  if (authState == 2) {
    soundPattern = soundHighChirp;
    soundPosition = 0;
    soundActive = true;
    cardUnlockActive = true;
    cardUnlockUntil = millis() + S_TO_MS(settings.cardUnlockTime);
    sendStatusPacket = true;
  } else if (authState == 3) {
    soundPattern = soundLow;
    soundPosition = 0;
    soundActive = true;
  }
}

void onCard(uint8_t uidLength, uint8_t uid[])
{
  int slot = -1;
  uint8_t authmessage[9];

  // TODO: check authState, handle a different card ID being presented

  soundPattern = soundMicroChirp;
  soundPosition = 0;
  soundActive = true;

  Serial.print("onCard ");
  Serial.print(uidLength);
  Serial.print(" ");
  for (uint8_t i = 0; i < uidLength; i++)
  {
    if (uid[i] < 16)
    {
      Serial.print("0");
    }
    Serial.print(uid[i], HEX);
  }
  Serial.println();

  if (authState == 0) {
    authState = 1;
    authStarted = millis();
    sendStatusPacket = true;
  }
}

void onCardRemoved()
{
  //Serial.println("onCardRemoved");
  authState = 0;
  sendStatusPacket = true;
}

void onMains()
{
  Serial.println("Now running on mains");
  sendStatusPacket = true;
}

void onBattery()
{
  Serial.println("Now running on battery");
  snibUnlockActive = false;
  sendStatusPacket = true;
}

void sendSlowStatus()
{
  static uint8_t macAddress[6];
  static uint32_t chipId;
  static uint32_t flashChipId;
  static uint32_t flashChipSize;
  static uint32_t flashChipSpeed;
  static uint16_t cardDatabaseSize;
  static boolean ready = false;
  uint32_t freeHeap;
  uint32_t now;

  if (!ready) {
    WiFi.macAddress(macAddress);
    chipId = ESP.getChipId(); chipId = PP_HTONL(chipId);
    flashChipId = ESP.getFlashChipId(); flashChipId = PP_HTONL(flashChipId);
    flashChipSize = ESP.getFlashChipSize(); flashChipSize = PP_HTONL(flashChipSize);
    flashChipSpeed = ESP.getFlashChipSpeed(); flashChipSpeed = PP_HTONL(flashChipSpeed);
    cardDatabaseSize = PP_HTONS(MAX_AUTHORIZED_CARDS);
    ready = true;
  }

  freeHeap = ESP.getFreeHeap(); freeHeap = PP_HTONL(freeHeap);
  now = millis(); now = PP_HTONL(now);

  Udp.write(0x01); Udp.write((const uint8_t*)&macAddress, 6);
  Udp.write(0x02); Udp.write((const uint8_t*)&chipId, 4);
  Udp.write(0x03); Udp.write((const uint8_t*)&flashChipId, 4);
  Udp.write(0x04); Udp.write((const uint8_t*)&flashChipSize, 4);
  Udp.write(0x05); Udp.write((const uint8_t*)&flashChipSpeed, 4);
  Udp.write(0x06); Udp.write((const uint8_t*)&cardDatabaseSize, 2);
  Udp.write(0x21); Udp.write((const uint8_t*)&freeHeap, 4);
  Udp.write(0x22); Udp.write((const uint8_t*)&now, 4);
  Udp.write(0x30); Udp.write((uint8_t)((pn532ResetCount & 0xFF00) >> 8)); Udp.write((uint8_t)(pn532ResetCount & 0xFF));
  Udp.write(0x31); Udp.write((uint8_t)((secondsOffline & 0xFF00) >> 8)); Udp.write((uint8_t)(secondsOffline & 0xFF));
  Udp.write(0x32); Udp.write((uint8_t)((wifiConnectionCount & 0xFF00) >> 8)); Udp.write((uint8_t)(wifiConnectionCount & 0xFF));
  Udp.write(0x33); Udp.write((uint8_t)((clientConnectionCount & 0xFF00) >> 8)); Udp.write((uint8_t)(clientConnectionCount & 0xFF));
  Udp.write(0x61); Udp.write((uint8_t)(settings.cardUnlockTime & 0xFF));
  Udp.write(0x62); Udp.write((uint8_t)(settings.exitUnlockMinTime & 0xFF));
  Udp.write(0x63); Udp.write((uint8_t)(settings.exitUnlockMaxTime & 0xFF));
  Udp.write(0x64); Udp.write((uint8_t)((settings.snibUnlockTime & 0xFF00) >> 8)); Udp.write((uint8_t)(settings.snibUnlockTime & 0xFF));
  Udp.write(0x65); Udp.write((uint8_t)(settings.pn532CheckInterval));
  Udp.write(0x66); Udp.write((uint8_t)(settings.authNetworkResendInterval));
  Udp.write(0x67); Udp.write((uint8_t)(settings.authNetworkTimeout));
  Udp.write(0x68); Udp.write((uint8_t)((settings.voltageScaleMultiplier & 0xFF00) >> 8)); Udp.write((uint8_t)(settings.voltageScaleMultiplier & 0xFF));
  Udp.write(0x69); Udp.write((uint8_t)((settings.voltageScaleDivider & 0xFF00) >> 8)); Udp.write((uint8_t)(settings.voltageScaleDivider & 0xFF));
  Udp.write(0x6A); Udp.write((uint8_t)((settings.voltageRisingThreshold & 0xFF00) >> 8)); Udp.write((uint8_t)(settings.voltageRisingThreshold & 0xFF));
  Udp.write(0x6B); Udp.write((uint8_t)((settings.voltageFallingThreshold & 0xFF00) >> 8)); Udp.write((uint8_t)(settings.voltageFallingThreshold & 0xFF));
  Udp.write(0x6C); Udp.write((uint8_t)settings.voltageCheckInterval);
  Udp.write(0x6D); Udp.write((uint8_t)settings.cardPresentTimeout);
  Udp.write(0x6E); Udp.write((uint8_t)settings.longPressTime);
  Udp.write(0x6F); Udp.write((uint8_t)settings.systemInfoInterval);
  Udp.write(0x70); Udp.write((uint8_t)settings.statusMinInterval);
  Udp.write(0x71); Udp.write((uint8_t)settings.statusMaxInterval);
  Udp.write(0x72); Udp.write((uint8_t)settings.doorAlarmTime);
  Udp.write(0x73); Udp.write((uint8_t)settings.errorSoundsEnabled);
  Udp.write(0x74); Udp.write((uint8_t)settings.allowSnibOnBattery);
  Udp.write(0x75); Udp.write((uint8_t)settings.helloFastInterval);
  Udp.write(0x76); Udp.write((uint8_t)settings.helloSlowInterval);
  Udp.write(0x77); Udp.write((uint8_t)settings.helloResponseTimeout);
}

void sendFastStatus()
{
  Udp.write(0x23); Udp.write((uint8_t)doorState);
  Udp.write(0x24); Udp.write((uint8_t)exitButtonState);
  Udp.write(0x25); Udp.write((uint8_t)snibButtonState);
  Udp.write(0x26); Udp.write((uint8_t)powerMode);
  Udp.write(0x27); Udp.write((uint8_t)((batteryVoltage & 0xFF00) >> 8)); Udp.write((uint8_t)(batteryVoltage & 0xFF));
  Udp.write(0x28); Udp.write((uint8_t)exitUnlockActive);
  Udp.write(0x29); Udp.write((uint8_t)snibUnlockActive);
  Udp.write(0x2A); Udp.write((uint8_t)cardUnlockActive);
  Udp.write(0x2C); Udp.write((uint8_t)authUidLen); Udp.write((const uint8_t*)&authUid[0], 7);
  Udp.write(0x2D); Udp.write((uint8_t)((batteryAdc & 0xFF00) >> 8)); Udp.write((uint8_t)(batteryAdc & 0xFF));
  Udp.write(0x2E); Udp.write((uint8_t)doorAjar);
  Udp.write(0x2F); Udp.write((uint8_t)eepromChangesPending);
  Udp.write(0x41); Udp.write((uint8_t)authState);
  Udp.write(0x42); Udp.write((uint8_t)snibEnabled);
  Udp.write(0x43); Udp.write((uint8_t)exitEnabled);
  Udp.write(0x44); Udp.write((uint8_t)doorForced);
}

void commitEeprom()
{
  Serial.print("Writing settings and database to EEPROM...");
  if (eepromChangesPending) {
    EEPROM.put(0, settings);
    EEPROM.put(EEPROM_CARD_DATABASE_LOCATION, cardDatabase);
    EEPROM.commit();
    eepromChangesPending = false;
    Serial.println(" done");
  } else {
    Serial.println(" no changes");
  }
}

/*******************************
 * timer-driver functions      *
 * (Serial should not be used) *
 *******************************/

void fastLoop()
{
  inputsLoop();
  ledLoop();
  soundLoop();

  if (authState == 1) {
    if (millis() - authStarted > DS_TO_MS(settings.authNetworkTimeout)) {
      onAuthTimeout();
    }
  }

  unlockLoop();


}

void unlockLoop()
{
  static boolean oldUnlockState = false;
  boolean newUnlockState = false;
  unsigned long now = millis();

  //Serial.print("now=");
  //Serial.println(now);

  if (cardUnlockActive) {
    //Serial.print("cardUnlockUntil=");
    //Serial.println(cardUnlockUntil);
    if (now <= cardUnlockUntil) {
      newUnlockState = true;
    } else {
      cardUnlockActive = false;
    }
  }

  if (exitUnlockActive) {
    //Serial.print("exitUnlockUntil=");
    //Serial.println(exitUnlockUntil);
    if (now <= exitUnlockUntil) {
      newUnlockState = true;
    } else {
      exitUnlockActive = false;
    }
  }

  if (snibUnlockActive) {
    //Serial.print("snibUnlockUntil=");
    //Serial.println(snibUnlockUntil);
    if (now <= snibUnlockUntil) {
      newUnlockState = true;
    } else {
      snibUnlockActive = false;
    }
  }

  if ((oldUnlockState == false) && (newUnlockState == true)) {
    //Serial.println("unlocked");
    digitalWrite(relayPin, HIGH);
    oldUnlockState = newUnlockState;
  } else if ((oldUnlockState == true) && (newUnlockState == false)) {
    //Serial.println("locked");
    digitalWrite(relayPin, LOW);
    oldUnlockState = newUnlockState;
  }

}

void inputsLoop()
{
  static int exitState = 0;
  static int snibState = 0;
  static int doorState = 0;
  static unsigned long snibLastPress;
  static bool snibLongPressed = false;
  static unsigned long exitLastPress;
  static bool exitLongPressed = false;
  uint8_t newStatus = 0;
  int tmp;

  tmp = digitalRead(exitPin);
  newStatus |= ((bool)tmp << 0);
  if ((exitState == LOW) && (tmp == HIGH)) {
    exitLongPressed = false;
    exitButtonUp();
  } else if ((exitState == HIGH) && (tmp == LOW)) {
    exitLastPress = millis();
    exitButtonDown();
  } else if ((tmp == LOW) && (millis() - exitLastPress > DS_TO_MS(settings.longPressTime)) && exitLongPressed == false) {
    exitLongPress();
    exitLongPressed = true;
  }
  exitState = tmp;

  tmp = digitalRead(snibPin);
  newStatus |= ((bool)tmp << 1);
  if ((snibState == LOW) && (tmp == HIGH)) {
    snibLongPressed = false;
    snibButtonUp();
  } else if ((snibState == HIGH) && (tmp == LOW)) {
    snibLastPress = millis();
    snibButtonDown();
  } else if ((tmp == LOW) && (millis() - snibLastPress > DS_TO_MS(settings.longPressTime)) && snibLongPressed == false) {
    snibLongPress();
    snibLongPressed = true;
  }
  snibState = tmp;

  tmp = digitalRead(doorPin);
  newStatus |= ((bool)tmp << 2);
  if ((doorState == LOW) && (tmp == HIGH)) {
    doorOpen();
  } else if ((doorState == HIGH) && (tmp == LOW)) {
    doorClosed();
  }
  doorState = tmp;
}

void ledLoop()
{
  static unsigned long lastChange = 0;
  static unsigned int lastMode = 0;
  static unsigned int lastState = 0;

  if (ledMode != lastMode) {
    // ledMode has changed
    // reset the local states
    lastMode = ledMode;
    lastState = 0;
    lastChange = millis();
  }

  switch (ledMode) {

    case LED_OFF:
      // off
      analogWrite(ledPin, 0);
      break;

    case LED_DIM:
      // dim
      analogWrite(ledPin, 200);
      break;

    case LED_ON:
      // full
      analogWrite(ledPin, 1023);
      break;

    case LED_BEACON:
      // beacon
      if (lastState == 0) {
        if (millis() - lastChange > 25) {
          analogWrite(ledPin, 0);
          lastState = 1;
          lastChange = millis();
        }
      } else {
        if (millis() - lastChange > 1225) {
          analogWrite(ledPin, 1023);
          lastState = 0;
          lastChange = millis();
        }
      }
      break;

    case LED_FAST:
      // fast flash
      if (lastState == 0) {
        if (millis() - lastChange > 40) {
          analogWrite(ledPin, 0);
          lastState = 1;
          lastChange = millis();
        }
      } else {
        if (millis() - lastChange > 40) {
          analogWrite(ledPin, 1023);
          lastState = 0;
          lastChange = millis();
        }
      }
      break;

    case LED_MEDIUM:
      // medium flash
      if (lastState == 0) {
        if (millis() - lastChange > 500) {
          analogWrite(ledPin, 0);
          lastState = 1;
          lastChange = millis();
        }
      } else {
        if (millis() - lastChange > 500) {
          analogWrite(ledPin, 1023);
          lastState = 0;
          lastChange = millis();
        }
      }
      break;

    case LED_SLOW:
      // slow flash
      if (lastState == 0) {
        if (millis() - lastChange > 1000) {
          analogWrite(ledPin, 0);
          lastState = 1;
          lastChange = millis();
        }
      } else {
        if (millis() - lastChange > 1000) {
          analogWrite(ledPin, 1023);
          lastState = 0;
          lastChange = millis();
        }
      }
      break;

  }

}


void soundLoop()
{
  static boolean playing = false;
  static unsigned long lastNote = 0;
  static boolean firstNote = false;

  if (playing==true && soundActive==false) {
    // stop playing
    //Serial.print(millis(), DEC);
    //Serial.println(" stopping");
    playing = false;
    soundPosition = 0;
    firstNote = false;
    pinMode(buzzerPin, INPUT);
    return;
  } else if (playing==false && soundActive==true) {
    // start playing
    //Serial.print(millis(), DEC);
    //Serial.println(" starting");
    playing = true;
    soundPosition = 0;
    firstNote = true;
    pinMode(buzzerPin, OUTPUT);
    analogWrite(buzzerPin, 1023);
  } else if (playing==false && soundActive==false) {
    return;
  }

  uint16_t currentFreq = soundPattern[soundPosition*2];
  uint16_t currentTime = soundPattern[(soundPosition*2)+1];

  if (firstNote) {
    //Serial.print(millis(), DEC);
    //Serial.print(" firstNote freq=");
    //Serial.print(currentFreq, DEC);
    //Serial.print(" time=");
    //Serial.println(currentTime, DEC);
    if (currentFreq==0) {
      analogWrite(buzzerPin, 1023);
    } else {
      analogWriteFreq(currentFreq);
      analogWrite(buzzerPin, 511);
    }
    lastNote = millis();
    firstNote = false;
    return;
  }

  if (millis()-lastNote > currentTime) {
    soundPosition++;
    /*
    if ((soundPosition*2)+1 >= sizeof(soundPattern)/2) {
      //Serial.println("end");
      pinMode(buzzerPin, INPUT);
      playing = false;
      soundPosition = 0;
      soundActive = false;
      return;
    }
    */
    currentFreq = soundPattern[soundPosition*2];
    currentTime = soundPattern[(soundPosition*2)+1];
    //Serial.print(millis(), DEC);
    //Serial.print(" pos=");
    //Serial.print(soundPosition, DEC);
    //Serial.print(" freq=");
    //Serial.print(currentFreq, DEC);
    //Serial.print(" time=");
    //Serial.println(currentTime, DEC);
    if (currentFreq==0 && currentTime==0) {
      //Serial.println("end");
      pinMode(buzzerPin, INPUT);
      playing = false;
      soundPosition = 0;
      soundActive = false;
      return;
    } else if (currentFreq==0xFFFF && currentTime==0xFFFF) {
      //Serial.println("loop");
      playing = false;
      soundPosition = 0;
      soundActive = true;
      return;
    } else {
      if (currentFreq==0) {
        analogWrite(buzzerPin, 1023);
      } else {
        analogWriteFreq(currentFreq);
        analogWrite(buzzerPin, 511);
      }
      lastNote = millis();
    }
  }

}

void onAuthTimeout()
{
  if (authState == 1) {
    //Serial.println("No response from network");
    boolean found = false;
    for (int i = 0; i < MAX_AUTHORIZED_CARDS; i++) {
      if (cardDatabase[i].uidlen==0x00 || cardDatabase[i].uidlen==0xFF) {
        // empty slot
        continue;
      }
      if (cardDatabase[i].uidlen == authUidLen) {
        if (memcmp(cardDatabase[i].uid, authUid, authUidLen) == 0) {
          //Serial.print("Local match found slot=");
          //Serial.println(i, DEC);
          found = true;
        }
      }
    }
    if (found) {
      authState = 4;
      sendStatusPacket = true;
      soundPattern = soundHighChirp;
      soundPosition = 0;
      soundActive = true;
      cardUnlockActive = true;
      cardUnlockUntil = millis() + S_TO_MS(settings.cardUnlockTime);
    } else {
      authState = 5;
      sendStatusPacket = true;
      soundPattern = soundLow;
      soundPosition = 0;
      soundActive = true;
    }
  }
}

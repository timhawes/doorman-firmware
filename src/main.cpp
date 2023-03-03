// SPDX-FileCopyrightText: 2019-2020 Tim Hawes
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <Hash.h>

#include <ArduinoJson.h>
#include <Buzzer.hpp>
#include <NFCReader.hpp>
#include <base64.hpp>

#include "AppConfig.hpp"
#include "Relay.hpp"
#include "VoltageMonitor.hpp"
#include "app_inputs.h"
#include "app_led.h"
#include "NetThing.hpp"
#include "app_setup.h"
#include "app_util.h"
#include "config.h"
#include "tokendb.hpp"

const uint8_t prog_buzzer_pin = 0;
const uint8_t door_pin = 14;
const uint8_t exit_pin = 4;
const uint8_t led_pin = 16;
const uint8_t pn532_reset_pin = 2;
const uint8_t relay_pin = 15;
const uint8_t scl_pin = 12;
const uint8_t sda_pin = 13;
const uint8_t snib_pin = 5;

char clientid[15];

AppConfig config;

PN532_I2C pn532i2c(Wire);
PN532 pn532(pn532i2c);
NFC nfc(pn532i2c, pn532, pn532_reset_pin);
NetThing net;
Buzzer buzzer(prog_buzzer_pin, true);
Inputs inputs(door_pin, exit_pin, snib_pin);
VoltageMonitor voltagemonitor;
Led led(led_pin);
Relay relay(relay_pin);

char pending_token[15];
unsigned long pending_token_time = 0;

bool firmware_restart_pending = false;
bool reset_pending = false;
bool restart_pending = false;

struct State {
  bool changed = true;
  bool card_enable = true;
  bool exit_enable = true;
  bool snib_enable = true;
  bool card_active = false;
  bool exit_active = false;
  bool snib_active = false;
  bool remote_active = false;
  bool unlock_active = false;
  unsigned long card_unlock_until = 0;
  unsigned long exit_unlock_until = 0;
  unsigned long snib_unlock_until = 0;
  unsigned long remote_unlock_until = 0;
  float voltage = 0;
  bool on_battery = false;
  bool door_open = false;
  bool network_up = false;
  char user[20] = "";
  char uid[15] = "";
  enum { auth_none, auth_online, auth_offline } auth;
} state;

WiFiEventHandler wifiEventConnectHandler;
WiFiEventHandler wifiEventDisconnectHandler;
bool wifi_connected = false;
bool network_connected = false;

Ticker token_lookup_timer;

bool status_updated = false;

buzzer_note network_tune[50];
buzzer_note ascending[] = { {1000, 250}, {1500, 250}, {2000, 250}, {0, 0} };

void send_state()
{
  DynamicJsonDocument obj(1024);
  obj["cmd"] = "state_info";
  obj["card_enable"] = state.card_enable;
  obj["card_active"] = state.card_active;
  obj["card_unlock_until"] = state.card_unlock_until;
  obj["exit_enable"] = state.exit_enable;
  obj["exit_active"] = state.exit_active;
  obj["exit_unlock_until"] = state.exit_unlock_until;
  obj["snib_enable"] = state.snib_enable;
  obj["snib_active"] = state.snib_active;
  obj["snib_unlock_until"] = state.snib_unlock_until;
  obj["remote_active"] = state.remote_active;
  obj["remote_unlock_until"] = state.remote_unlock_until;
  obj["unlock"] = state.unlock_active;
  obj["voltage"] = state.voltage;
  obj["user"] = state.user;
  obj["uid"] = state.uid;
  switch (state.auth) {
    case state.auth_none:
      obj["auth"] = (char*)0;
      break;
    case state.auth_online:
      obj["auth"] = "online";
      break;
    case state.auth_offline:
      obj["auth"] = "offline";
      break;
  }
  if (state.door_open) {
    obj["door"] = "open";
  } else {
    obj["door"] = "closed";
  }
  if (state.on_battery) {
    obj["power"] = "battery";
  } else {
    obj["power"] = "mains";
  }
  obj.shrinkToFit();
  net.sendJson(obj);

  status_updated = false;
}

void check_leds()
{
  if (state.card_active || state.exit_active) {
    led.flash_fast();
  } else if (state.snib_active || state.remote_active) {
    led.flash_medium();
  } else if (state.on_battery) {
    led.dim();
  } else if (state.network_up == false) {
    led.blink();
  } else {
    led.on();
  }
}

void handle_timeouts()
{
  if (state.card_active && millis() > state.card_unlock_until) {
    Serial.println("card unlock expired");
    state.card_active = false;
    state.auth = state.auth_none;
    strncpy(state.user, "", sizeof(state.user));
    strncpy(state.uid, "", sizeof(state.uid));
    state.changed = true;
  }
  if (state.exit_active && millis() > state.exit_unlock_until) {
    Serial.println("exit unlock expired");
    state.exit_active = false;
    state.changed = true;
  }
  if (state.snib_active && millis() > state.snib_unlock_until) {
    Serial.println("snib unlock expired");
    state.snib_active = false;
    state.changed = true;
  }
  if (state.remote_active && millis() > state.remote_unlock_until) {
    Serial.println("remote unlock expired");
    state.remote_active = false;
    state.changed = true;
  }
}

void check_state()
{
  if (state.card_active || state.exit_active || state.snib_active || state.remote_active) {
    if (!state.unlock_active) {
      relay.active(true);
      state.unlock_active = true;
      net.sendEvent("unlocked");
    }
  } else {
    if (state.unlock_active) {
      relay.active(false);
      state.unlock_active = false;
      net.sendEvent("locked");
    }
  }

  state.changed = false;
  check_leds();
}

void token_info_callback(const char *uid, bool found, const char *name, uint8_t access)
{
  token_lookup_timer.detach();

  Serial.print("token_info_callback: time=");
  Serial.println(millis()-pending_token_time, DEC);

  if (!state.card_enable) {
    buzzer.beep(500, 256);
    return;
  }

  if (found) {
    if (access > 0) {
      state.card_active = true;
      state.card_unlock_until = millis() + config.card_unlock_time;
      strncpy(state.user, name, sizeof(state.user));
      strncpy(state.uid, uid, sizeof(state.uid));
      state.auth = state.auth_online;
      state.changed = true;
      buzzer.beep(100, 1000);
      net.sendEvent("auth", 128, "uid=%s user=%s type=online access=granted", state.uid, state.user);
    } else {
      buzzer.beep(500, 256);
      net.sendEvent("auth", 128, "uid=%s user=%s type=online access=denied", uid, name);
    }
    return;
  }

  TokenDB tokendb("tokens.dat");
  if (tokendb.lookup(uid)) {
    if (tokendb.get_access_level() > 0) {
      state.card_active = true;
      state.card_unlock_until = millis() + config.card_unlock_time;
      strncpy(state.user, tokendb.get_user().c_str(), sizeof(state.user));
      strncpy(state.uid, uid, sizeof(state.uid));
      state.auth = state.auth_offline;
      state.changed = true;
      buzzer.beep(100, 1000);
      net.sendEvent("auth", 128, "uid=%s user=%s type=offline access=granted", uid, state.user);
      return;
    }
  }

  buzzer.beep(500, 256);

  net.sendEvent("auth", 128, "uid=%s user=%s type=offline access=denied", uid, name);

  return;
}

void token_present(NFCToken token)
{
  Serial.print("token_present: ");
  Serial.println(token.uidString());
  buzzer.beep(100, 500);

  DynamicJsonDocument obj(2048);
  obj["cmd"] = "token_auth";
  obj["uid"] = token.uidString();
  if (token.ats_len > 0) {
    obj["ats"] = hexlify(token.ats, token.ats_len);
  }
  obj["atqa"] = (int)token.atqa;
  obj["sak"] = (int)token.sak;
  if (token.version_len > 0) {
    obj["version"] = hexlify(token.version, token.version_len);
  }
  if (token.ntag_counter > 0) {
    obj["ntag_counter"] = (long)token.ntag_counter;
  }
  if (token.ntag_signature_len > 0) {
    obj["ntag_signature"] = hexlify(token.ntag_signature, token.ntag_signature_len);
  }
  if (token.data_len > 0) {
    obj["data"] = hexlify(token.data, token.data_len);
  }
  if (token.read_time > 0) {
    obj["read_time"] = token.read_time;
  }
  obj.shrinkToFit();

  strncpy(pending_token, token.uidString().c_str(), sizeof(pending_token));
  token_lookup_timer.once_ms(config.token_query_timeout, std::bind(&token_info_callback, pending_token, false, "", 0));

  pending_token_time = millis();
  net.sendJson(obj, true);
  net.sendEvent("token", 64, "uid=%s", pending_token);
}

void token_removed(NFCToken token)
{
  Serial.print("token_removed: ");
  Serial.println(token.uidString());
}

void load_config()
{
  config.LoadJson();
  inputs.set_long_press_time(config.long_press_time);
  led.setDimLevel(config.led_dim);
  led.setBrightLevel(config.led_bright);
  net.setWiFi(config.ssid, config.wpa_password);
  net.setServer(config.server_host, config.server_port,
                config.server_tls_enabled, config.server_tls_verify,
                config.server_fingerprint1, config.server_fingerprint2);
  net.setCred(clientid, config.server_password);
  net.setDebug(config.dev);
  net.setWatchdog(config.network_watchdog_time);
  nfc.read_counter = config.nfc_read_counter;
  nfc.read_data = config.nfc_read_data;
  nfc.read_sig = config.nfc_read_sig;
  nfc.pn532_check_interval = config.nfc_check_interval;
  nfc.pn532_reset_interval = config.nfc_reset_interval;
  nfc.per_5s_limit = config.nfc_5s_limit;
  nfc.per_1m_limit = config.nfc_1m_limit;
  relay.setInvert(config.invert_relay);
  voltagemonitor.set_interval(config.voltage_check_interval);
  voltagemonitor.set_ratio(config.voltage_multiplier);
  voltagemonitor.set_threshold(config.voltage_falling_threshold, config.voltage_rising_threshold);
  state.changed = true;
}

void door_open_callback()
{
  Serial.println("door-open");
  if (config.anti_bounce) {
    if (state.exit_active) {
      state.exit_active = false;
    }
    if (state.card_active) {
      state.card_active = false;
      state.auth = state.auth_none;
      strncpy(state.user, "", sizeof(state.user));
      strncpy(state.uid, "", sizeof(state.uid));
    }
  }
  state.door_open = true;
  state.changed = true;
  net.sendEvent("door_open");
}

void door_close_callback()
{
  Serial.println("door-close");
  state.door_open = false;
  state.changed = true;
  net.sendEvent("door_closed");
}

void exit_press_callback()
{
  Serial.println("exit-press");
  if (state.exit_enable) {
    state.exit_active = true;
    state.exit_unlock_until = millis() + config.exit_unlock_time;
    state.changed = true;
    net.sendEvent("exit_request");
  } else {
    net.sendEvent("exit_request_ignored");
  }
}

void exit_longpress_callback()
{
  Serial.println("exit-longpress");
  if (config.hold_exit_for_snib) {
    if (state.snib_active) {
      buzzer.beep(100, 500);
      state.snib_active = false;
      state.exit_active = false;
      state.changed = true;
      net.sendEvent("snib_on");
    } else {
      if (state.snib_enable && (state.on_battery == false || config.allow_snib_on_battery)) {
        buzzer.beep(100, 1000);
        state.snib_active = true;
        state.snib_unlock_until = millis () + config.snib_unlock_time;
        state.exit_active = false;
        state.changed = true;
        net.sendEvent("snib_on");
      }
    }
  }
}

void exit_release_callback()
{
  Serial.println("exit-release");

  // handle exit button interactive mode
  if (state.exit_active) {
    if (config.exit_interactive_time > 0) {
      state.exit_unlock_until = millis() + config.exit_interactive_time;
    }
  }
}

void snib_press_callback()
{
  Serial.println("snib-press");
  if (state.snib_active) {
    state.snib_active = false;
    state.changed = true;
    net.sendEvent("snib_off");
  } else {
    if (state.snib_enable && (state.on_battery == false || config.allow_snib_on_battery)) {
      state.snib_active = true;
      state.snib_unlock_until = millis () + config.snib_unlock_time;
      state.changed = true;
      net.sendEvent("snib_on");
    }
  }
}

void snib_longpress_callback()
{
  Serial.println("snib-longpress");
}

void snib_release_callback()
{
  Serial.println("snib-release");
}

void on_battery_callback()
{
  Serial.println("on battery");
  state.on_battery = true;
  state.changed = true;
  net.sendEvent("power_battery");
}

void on_mains_callback()
{
  Serial.println("on mains");
  state.on_battery = false;
  state.changed = true;
  net.sendEvent("power_mains");
}

void voltage_callback(float voltage)
{
  state.voltage = voltage;
  //state.changed = true;
}

void wifi_connect_callback(const WiFiEventStationModeGotIP& event)
{
  wifi_connected = true;
  state.network_up = wifi_connected && network_connected;
  state.changed = true;
}

void wifi_disconnect_callback(const WiFiEventStationModeDisconnected& event)
{
  wifi_connected = false;
  state.network_up = wifi_connected && network_connected;
  state.changed = true;
}

void network_connect_callback()
{
  network_connected = true;
  state.network_up = wifi_connected && network_connected;
  state.changed = true;
}

void network_disconnect_callback()
{
  network_connected = false;
  state.network_up = wifi_connected && network_connected;
  state.changed = true;
}

void network_restart_callback(bool immediate, bool firmware)
{
  if (immediate) {
    ESP.reset();
    delay(5000);
  }
  if (firmware) {
    firmware_restart_pending = true;
  } else {
    restart_pending = true;
  }
}

void network_transfer_status_callback(const char *filename, int progress, bool active, bool changed)
{
  static int previous_progress = 0;
  if (strcmp("firmware", filename) == 0) {
    if (previous_progress != progress) {
      Serial.print("firmware install ");
      Serial.print(progress, DEC);
      Serial.println("%");
      previous_progress = progress;
    }
  }
  if (changed && strcmp("config.json", filename) == 0) {
    load_config();
  }
}

/*************************************************************************
 * NETWORK COMMANDS                                                      *
 *************************************************************************/

void network_cmd_buzzer_beep(const JsonDocument &obj)
{
  if (obj["hz"]) {
    buzzer.beep(obj["ms"].as<int>(), obj["hz"].as<int>());
  } else {
    buzzer.beep(obj["ms"].as<int>());
  }
}

void network_cmd_buzzer_chirp(const JsonDocument &obj)
{
  buzzer.chirp();
}

void network_cmd_buzzer_click(const JsonDocument &obj)
{
  buzzer.click();
}

void network_cmd_buzzer_tune(const JsonDocument &obj)
{
  const char *b64 = obj["data"].as<const char*>();
  unsigned int binary_length = decode_base64_length((unsigned char*)b64);
  uint8_t binary[binary_length];
  binary_length = decode_base64((unsigned char*)b64, binary);

  memset(network_tune, 0, sizeof(network_tune));
  if (binary_length > sizeof(network_tune)) {
    memcpy(network_tune, binary, sizeof(network_tune));
  } else {
    memcpy(network_tune, binary, binary_length);
  }

  buzzer.play(network_tune);
}

void network_cmd_metrics_query(const JsonDocument &obj)
{
  DynamicJsonDocument reply(512);
  reply["cmd"] = "metrics_info";
  reply["millis"] = millis();
  reply["nfc_reset_count"] = nfc.reset_count;
  reply["nfc_token_count"] = nfc.token_count;
  reply.shrinkToFit();
  net.sendJson(reply);
}

void network_cmd_state_query(const JsonDocument &obj)
{
  send_state();
}

void network_cmd_state_set(const JsonDocument &obj)
{
  if (obj.containsKey("card_enable")) {
    state.card_enable = obj["card_enable"];
  }
  if (obj.containsKey("exit_enable")) {
    state.exit_enable = obj["exit_enable"];
  }
  if (obj.containsKey("snib_enable")) {
    state.snib_enable = obj["snib_enable"];
  }
  if (obj.containsKey("card_active")) {
    state.card_active = obj["card_active"];
    if (state.card_active) {
      state.card_unlock_until = millis() + config.card_unlock_time;
      buzzer.beep(100, 1000);
    }
  }
  if (obj.containsKey("exit_active")) {
    state.exit_active = obj["exit_active"];
    if (state.exit_active) {
      state.exit_unlock_until = millis() + config.exit_unlock_time;
    }
  }
  if (obj.containsKey("snib_active")) {
    state.snib_active = obj["snib_active"];
    if (state.snib_active) {
      state.snib_unlock_until = millis() + config.snib_unlock_time;
    }
  }
  if (obj.containsKey("remote_active")) {
    state.remote_active = obj["remote_active"];
    if (state.remote_active) {
      state.remote_unlock_until = millis() + config.remote_unlock_time;
    }
  }
  if (obj.containsKey("user")) {
    strncpy(state.user, obj["user"], sizeof(state.user));
  }
  if (obj.containsKey("uid")) {
    strncpy(state.uid, obj["uid"], sizeof(state.uid));
  }
  if (obj.containsKey("snib_renew")) {
    if (state.snib_active) {
      state.snib_unlock_until = millis() + config.snib_unlock_time;
    }
  }
  state.changed = true;
}

void network_cmd_token_info(const JsonDocument &obj)
{
  token_info_callback(obj["uid"], obj["found"], obj["name"], obj["access"]);
}

void network_message_callback(const JsonDocument &obj)
{
  String cmd = obj["cmd"];

  if (cmd == "buzzer_beep") {
    network_cmd_buzzer_beep(obj);
  } else if (cmd == "buzzer_chirp") {
    network_cmd_buzzer_chirp(obj);
  } else if (cmd == "buzzer_click") {
    network_cmd_buzzer_click(obj);
  } else if (cmd == "buzzer_tune") {
    network_cmd_buzzer_tune(obj);
  } else if (cmd == "metrics_query") {
    network_cmd_metrics_query(obj);
  } else if (cmd == "state_query") {
    network_cmd_state_query(obj);
  } else if (cmd == "state_set") {
    network_cmd_state_set(obj);
  } else if (cmd == "token_info") {
    network_cmd_token_info(obj);
  } else {
    StaticJsonDocument<JSON_OBJECT_SIZE(3)> reply;
    reply["cmd"] = "error";
    reply["requested_cmd"] = cmd.c_str();
    reply["error"] = "not implemented";
    net.sendJson(reply);
  }
}

void setup()
{
  pinMode(pn532_reset_pin, OUTPUT);
  digitalWrite(pn532_reset_pin, HIGH);

  // initial use of the shared pin will be for detecting setup mode
  pinMode(prog_buzzer_pin, INPUT_PULLUP);

  snprintf(clientid, sizeof(clientid), "doorman-%06x", ESP.getChipId());
  WiFi.hostname(String(clientid));

  wifiEventConnectHandler = WiFi.onStationModeGotIP(wifi_connect_callback);
  wifiEventDisconnectHandler = WiFi.onStationModeDisconnected(wifi_disconnect_callback);

  Serial.begin(115200);
  for (int i=0; i<1024; i++) {
    Serial.print(" \b");
  }
  Serial.println();

  Serial.print(clientid);
  Serial.print(" ");
  Serial.println(ESP.getSketchMD5());

  Wire.begin(sda_pin, scl_pin);

  Serial.print("SPIFFS: ");
  if (SPIFFS.begin()) {
    FSInfo fs_info;
    SPIFFS.info(fs_info);
    Serial.print("ready, used=");
    Serial.print(fs_info.usedBytes, DEC);
    Serial.print(" total=");
    Serial.println(fs_info.totalBytes, DEC);
  } else {
    Serial.println("failed");
  }

  unsigned long start_time = millis();
  while (millis() - start_time < 500) {
    if (digitalRead(prog_buzzer_pin) == LOW) {
      Serial.println("prog button pressed, going into setup mode");
      SetupMode setup_mode(clientid, setup_password);
      setup_mode.run();
      ESP.restart();
    }
  }

  // setup mode detection finished
  // configure for buzzer output and default LOW to silence PSU noise
  pinMode(prog_buzzer_pin, OUTPUT);
  digitalWrite(prog_buzzer_pin, LOW);

  if (SPIFFS.exists("config.json")) {
    load_config();
  } else {
    Serial.println("config.json is missing, entering setup mode");
    net.stop();
    delay(1000);
    SetupMode setup_mode(clientid, setup_password);
    setup_mode.run();
    ESP.restart();
  }

  led.begin();

  net.onConnect(network_connect_callback);
  net.onDisconnect(network_disconnect_callback);
  net.onRestartRequest(network_restart_callback);
  net.onReceiveJson(network_message_callback);
  net.onTransferStatus(network_transfer_status_callback);
  net.setCommandKey("cmd");
  net.start();

  inputs.door_close_callback = door_close_callback;
  inputs.door_open_callback = door_open_callback;
  inputs.exit_press_callback = exit_press_callback;
  inputs.exit_longpress_callback = exit_longpress_callback;
  inputs.exit_release_callback = exit_release_callback;
  inputs.snib_press_callback = snib_press_callback;
  inputs.snib_longpress_callback = snib_longpress_callback;
  inputs.snib_release_callback = snib_release_callback;
  inputs.begin();

  nfc.token_present_callback = token_present;
  nfc.token_removed_callback = token_removed;

  voltagemonitor.on_battery_callback = on_battery_callback;
  voltagemonitor.on_mains_callback = on_mains_callback;
  voltagemonitor.voltage_callback = voltage_callback;
  voltagemonitor.begin();
}

void loop() {
  static unsigned long last_timeout_check = 0;

  nfc.loop();

  yield();

  inputs.loop();

  yield();

  net.loop();

  yield();

  if (millis() - last_timeout_check > 200) {
    handle_timeouts();
    last_timeout_check = millis();
  }

  if (state.changed) {
    check_state();
    send_state();
  }

  yield();

  if (firmware_restart_pending) {
    Serial.println("restarting to complete firmware install...");
    net.stop();
    led.off();
    delay(1000);
    Serial.println("restarting now!");
    ESP.restart();
    delay(5000);
  }

  if (reset_pending || restart_pending) {
    Serial.println("rebooting at remote request...");
    net.stop();
    led.off();
    delay(1000);
    if (reset_pending) {
      Serial.println("resetting now!");
      ESP.reset();
    }
    if (restart_pending) {
      Serial.println("restarting now!");
      ESP.restart();
    }
    delay(5000);
  }

}

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include <Hash.h>

#include <ArduinoJson.h>
#include <Buzzer.hpp>
#include <NFCReader.hpp>
#include <base64.hpp>

#include "AppConfig.hpp"
#include "FileWriter.hpp"
#include "FirmwareWriter.hpp"
#include "Relay.hpp"
#include "VoltageMonitor.hpp"
#include "app_inputs.h"
#include "app_led.h"
#include "app_network.h"
#include "app_setup.h"
#include "app_util.h"
#include "config.h"
#include "tokendb.hpp"

const uint8_t buzzer_pin = 0;
const uint8_t door_pin = 14;
const uint8_t exit_pin = 4;
const uint8_t prog_pin = 0;
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
Network net;
Buzzer buzzer(buzzer_pin, true);
Inputs inputs(door_pin, exit_pin, snib_pin);
VoltageMonitor voltagemonitor;
Led led(led_pin);
Relay relay(relay_pin);

char pending_token[15];
unsigned long pending_token_time = 0;

unsigned long last_network_activity = 0;

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
} state;

Ticker token_lookup_timer;
Ticker file_timeout_timer;
Ticker firmware_timeout_timer;

bool status_updated = false;

FileWriter file_writer;
FirmwareWriter firmware_writer;

buzzer_note network_tune[50];
buzzer_note ascending[] = { {1000, 250}, {1500, 250}, {2000, 250}, {0, 0} };

void send_state()
{
  DynamicJsonBuffer jb;
  JsonObject &obj = jb.createObject();
  obj["cmd"] = "state_info";
  obj["card_enable"] = state.card_enable;
  obj["card_active"] = state.card_active;
  obj["exit_enable"] = state.exit_enable;
  obj["exit_active"] = state.exit_active;
  obj["snib_enable"] = state.snib_enable;
  obj["snib_active"] = state.snib_active;
  obj["remote_active"] = state.remote_active;
  obj["unlock"] = state.unlock_active;
  obj["voltage"] = state.voltage;
  obj["user"] = state.user;
  obj["uid"] = state.uid;
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
  net.send_json(obj);

  status_updated = false;
}

void check_leds()
{
  Serial.println("check_leds");
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
  static bool previous_on_battery = false;
  static bool first_run = true;
  bool changed = false;

  if (state.card_active || state.exit_active || state.snib_active || state.remote_active) {
    if (!state.unlock_active) {
      relay.active(true);
      state.unlock_active = true;
      changed = true;
    }
  } else {
    if (state.unlock_active) {
      relay.active(false);
      state.unlock_active = false;
      changed = true;
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
      state.changed = true;
      buzzer.beep(100, 1000);
    } else {
      buzzer.beep(500, 256);
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
      state.changed = true;
      buzzer.beep(100, 1000);
      return;
    }
  }

  buzzer.beep(500, 256);
  return;
}

void token_present(NFCToken token)
{
  Serial.print("token_present: ");
  Serial.println(token.uidString());
  buzzer.beep(100, 500);
  
  DynamicJsonBuffer jb;
  JsonObject &obj = jb.createObject();
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
  
  strncpy(pending_token, token.uidString().c_str(), sizeof(pending_token));
  token_lookup_timer.once_ms(config.token_query_timeout, std::bind(&token_info_callback, pending_token, false, "", 0));

  pending_token_time = millis();
  net.send_json(obj, true);
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
  net.set_wifi(config.ssid, config.wpa_password);
  net.set_server(config.server_host, config.server_port, config.server_password,
                 config.server_tls_enabled, config.server_tls_verify,
                 config.server_fingerprint1, config.server_fingerprint2);
  nfc.read_counter = config.nfc_read_counter;
  nfc.read_data = config.nfc_read_data;
  nfc.read_sig = config.nfc_read_sig;
  relay.setInvert(config.invert_relay);
  voltagemonitor.set_interval(config.voltage_check_interval);
  voltagemonitor.set_ratio(config.voltage_multiplier);
  voltagemonitor.set_threshold(config.voltage_falling_threshold, config.voltage_rising_threshold);
  state.changed = true;
}

void send_file_info(const char *filename)
{
  DynamicJsonBuffer jb;
  JsonObject &obj = jb.createObject();
  obj["cmd"] = "file_info";
  obj["filename"] = filename;

  File f = SPIFFS.open(filename, "r");
  if (f) {
    MD5Builder md5;
    md5.begin();
    while (f.available()) {
      uint8_t buf[256];
      size_t buflen;
      buflen = f.readBytes((char*)buf, 256);
      md5.add(buf, buflen);
    }
    md5.calculate();
    obj["size"] = f.size();
    obj["md5"] = md5.toString();
    f.close();
  } else {
    obj["size"] = (char*)NULL;
    obj["md5"] = (char*)NULL;
  }

  net.send_json(obj);
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
      strncpy(state.user, "", sizeof(state.user));
      strncpy(state.uid, "", sizeof(state.uid));
    }
  }
  state.door_open = true;
  state.changed = true;
}

void door_close_callback()
{
  Serial.println("door-close");
  state.door_open = false;
  state.changed = true;
}

void exit_press_callback()
{
  Serial.println("exit-press");
  if (state.exit_enable) {
    state.exit_active = true;
    state.exit_unlock_until = millis() + config.exit_unlock_time;
    state.changed = true;
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
    } else {
      if (state.snib_enable && (state.on_battery == false || config.allow_snib_on_battery)) {
        buzzer.beep(100, 1000);
        state.snib_active = true;
        state.snib_unlock_until = millis () + config.snib_unlock_time;
        state.exit_active = false;
        state.changed = true;
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
  } else {
    if (state.snib_enable && (state.on_battery == false || config.allow_snib_on_battery)) {
      state.snib_active = true;
      state.snib_unlock_until = millis () + config.snib_unlock_time;
      state.changed = true;
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

void firmware_status_callback(bool active, bool restart, unsigned int progress)
{
  static unsigned int previous_progress = 0;
  if (previous_progress != progress) {
    Serial.print("firmware install ");
    Serial.print(progress, DEC);
    Serial.println("%");
    previous_progress = progress;
  }
  if (restart) {
    firmware_restart_pending = true;
  }
}

void file_timeout_callback()
{
  file_writer.Abort();

  DynamicJsonBuffer jb;
  JsonObject &root = jb.createObject();
  root["cmd"] = "file_write_error";
  root["error"] = "file write timed-out";
  net.send_json(root);
}

void firmware_timeout_callback()
{
  file_writer.Abort();

  DynamicJsonBuffer jb;
  JsonObject &root = jb.createObject();
  root["cmd"] = "firmware_write_error";
  root["error"] = "firmware write timed-out";
  net.send_json(root);
}

void set_file_timeout(bool enabled) {
  if (enabled) {
    file_timeout_timer.once(60, file_timeout_callback);
  } else {
    file_timeout_timer.detach();
  }
}

void set_firmware_timeout(bool enabled) {
  if (enabled) {
    firmware_timeout_timer.once(60, firmware_timeout_callback);
  } else {
    firmware_timeout_timer.detach();
  }
}

void network_state_callback(bool wifi_up, bool tcp_up, bool ready)
{
  Serial.println("network_state_callback");
  if (wifi_up && tcp_up && ready) {
    state.network_up = true;
    state.changed = true;
  } else {
    state.network_up = false;
    state.changed = true;
  }
}

void on_battery_callback()
{
  Serial.println("on battery");
  state.on_battery = true;
  state.changed = true;
}

void on_mains_callback()
{
  Serial.println("on mains");
  state.on_battery = false;
  state.changed = true;
}

void voltage_callback(float voltage)
{
  state.voltage = voltage;
  //state.changed = true;
}

/*************************************************************************
 * NETWORK COMMANDS                                                      *
 *************************************************************************/

void network_cmd_buzzer_beep(JsonObject &obj)
{
  if (obj["hz"]) {
    buzzer.beep(obj["ms"].as<int>(), obj["hz"].as<int>());
  } else {
    buzzer.beep(obj["ms"].as<int>());
  }
}

void network_cmd_buzzer_chirp(JsonObject &obj)
{
  buzzer.chirp();
}

void network_cmd_buzzer_click(JsonObject &obj)
{
  buzzer.click();
}

void network_cmd_buzzer_tune(JsonObject &obj)
{
  const char *b64 = obj.get<const char*>("data");
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

void network_cmd_file_data(JsonObject &obj)
{
  const char *b64 = obj.get<const char*>("data");
  unsigned int binary_length = decode_base64_length((unsigned char*)b64);
  uint8_t binary[binary_length];
  binary_length = decode_base64((unsigned char*)b64, binary);

  set_file_timeout(false);

  DynamicJsonBuffer jb;
  JsonObject &reply = jb.createObject();

  if (file_writer.Add(binary, binary_length, obj["position"])) {
    if (obj["eof"].as<bool>() == 1) {
      if (file_writer.Commit()) {
        // finished and successful
        reply["cmd"] = "file_write_ok";
        reply["filename"] = obj["filename"];
        net.send_json(reply);
        send_file_info(obj["filename"]);
        if (obj["filename"] == "config.json") {
          load_config();
        }
      } else {
        // finished but commit failed
        reply["cmd"] = "file_write_error";
        reply["filename"] = obj["filename"];
        reply["error"] = "file_writer.Commit() failed";
        net.send_json(reply);
      }
    } else {
      // more data required
      reply["cmd"] = "file_continue";
      reply["filename"] = obj["filename"];
      reply["position"] = obj["position"].as<int>() + binary_length;
      net.send_json(reply, true);
    }
  } else {
    reply["cmd"] = "file_write_error";
    reply["filename"] = obj["filename"];
    reply["error"] = "file_writer.Add() failed";
    net.send_json(reply);
  }

  set_file_timeout(file_writer.Running());
}

void network_cmd_file_delete(JsonObject &obj)
{
  DynamicJsonBuffer jb;
  JsonObject &reply = jb.createObject();

  if (SPIFFS.remove((const char*)obj["filename"])) {
    reply["cmd"] = "file_delete_ok";
    reply["filename"] = obj["filename"];
    net.send_json(reply);
  } else {
    reply["cmd"] = "file_delete_error";
    reply["error"] = "failed to delete file";
    reply["filename"] = obj["filename"];
    net.send_json(reply);
  }
}

void network_cmd_file_dir_query(JsonObject &obj)
{
  DynamicJsonBuffer jb;
  JsonObject &reply = jb.createObject();
  JsonArray &files = jb.createArray();
  reply["cmd"] = "file_dir_info";
  reply["path"] = obj["path"];
  if (SPIFFS.exists((const char*)obj["path"])) {
    Dir dir = SPIFFS.openDir((const char*)obj["path"]);
    while (dir.next()) {
      files.add(dir.fileName());
    }
    reply["filenames"] = files;
  } else {
    reply["filenames"] = (char*)NULL;
  }
  net.send_json(reply);
}

void network_cmd_file_query(JsonObject &obj)
{
  send_file_info(obj["filename"]);
}

void network_cmd_file_rename(JsonObject &obj)
{
  DynamicJsonBuffer jb;
  JsonObject &reply = jb.createObject();

  if (SPIFFS.rename((const char*)obj["old_filename"], (const char*)obj["new_filename"])) {
    reply["cmd"] = "file_rename_ok";
    reply["old_filename"] = obj["old_filename"];
    reply["new_filename"] = obj["new_filename"];
    net.send_json(reply);
  } else {
    reply["cmd"] = "file_rename_error";
    reply["error"] = "failed to rename file";
    reply["old_filename"] = obj["old_filename"];
    reply["new_filename"] = obj["new_filename"];
    net.send_json(reply);
  }
}

void network_cmd_file_write(JsonObject &obj)
{
  DynamicJsonBuffer jb;
  JsonObject &reply = jb.createObject();

  set_file_timeout(false);

  if (file_writer.Begin(obj["filename"], obj["md5"], obj["size"])) {
    if (file_writer.UpToDate()) {
        reply["cmd"] = "file_write_error";
        reply["filename"] = obj["filename"];
        reply["error"] = "already up to date";
        net.send_json(reply);
    } else {
      if (file_writer.Open()) {
        reply["cmd"] = "file_continue";
        reply["filename"] = obj["filename"];
        reply["position"] = 0;
        net.send_json(reply);
      } else {
        reply["cmd"] = "file_write_error";
        reply["filename"] = obj["filename"];
        reply["error"] = "file_writer.Open() failed";
        net.send_json(reply);
      }
    }
  } else {
    reply["cmd"] = "file_write_error";
    reply["filename"] = obj["filename"];
    reply["error"] = "file_writer.Begin() failed";
    net.send_json(reply);
  }

  set_file_timeout(file_writer.Running());
}

void network_cmd_firmware_data(JsonObject &obj)
{
  const char *b64 = obj.get<const char*>("data");
  unsigned int binary_length = decode_base64_length((unsigned char*)b64);
  uint8_t binary[binary_length];
  binary_length = decode_base64((unsigned char*)b64, binary);

  set_firmware_timeout(false);

  DynamicJsonBuffer jb;
  JsonObject &reply = jb.createObject();

  if (firmware_writer.Add(binary, binary_length, obj["position"])) {
    if (obj["eof"].as<bool>() == 1) {
      if (firmware_writer.Commit()) {
        // finished and successful
        reply["cmd"] = "firmware_write_ok";
        net.send_json(reply);
        firmware_status_callback(false, true, 100);
      } else {
        // finished but commit failed
        reply["cmd"] = "firmware_write_error";
        reply["error"] = "firmware_writer.Commit() failed";
        reply["updater_error"] = firmware_writer.GetUpdaterError();
        net.send_json(reply);
        firmware_status_callback(false, false, 0);
      }
    } else {
      // more data required
      reply["cmd"] = "firmware_continue";
      reply["position"] = obj["position"].as<int>() + binary_length;
      net.send_json(reply, true);
      firmware_status_callback(true, false, firmware_writer.Progress());
    }
  } else {
    reply["cmd"] = "firmware_write_error";
    reply["error"] = "firmware_writer.Add() failed";
    reply["updater_error"] = firmware_writer.GetUpdaterError();
    net.send_json(reply);
    firmware_status_callback(false, false, 0);
  }

  set_firmware_timeout(firmware_writer.Running());
}

void network_cmd_firmware_write(JsonObject &obj)
{
  DynamicJsonBuffer jb;
  JsonObject &reply = jb.createObject();

  set_firmware_timeout(false);

  if (firmware_writer.Begin(obj["md5"], obj["size"])) {
    if (firmware_writer.UpToDate()) {
      reply["cmd"] = "firmware_write_error";
      reply["md5"] = obj["md5"];
      reply["error"] = "already up to date";
      reply["updater_error"] = firmware_writer.GetUpdaterError();
      net.send_json(reply);
    } else {
      if (firmware_writer.Open()) {
        reply["cmd"] = "firmware_continue";
        reply["md5"] = obj["md5"];
        reply["position"] = 0;
        net.send_json(reply);
      } else {
        reply["cmd"] = "firmware_write_error";
        reply["md5"] = obj["md5"];
        reply["error"] = "firmware_writer.Open() failed";
        reply["updater_error"] = firmware_writer.GetUpdaterError();
        net.send_json(reply);
      }
    }
  } else {
    reply["cmd"] = "firmware_write_error";
    reply["md5"] = obj["md5"];
    reply["error"] = "firmware_writer.Begin() failed";
    reply["updater_error"] = firmware_writer.GetUpdaterError();
    net.send_json(reply);
  }

  set_firmware_timeout(firmware_writer.Running());
}

void network_cmd_metrics_query(JsonObject &obj)
{
  DynamicJsonBuffer jb;
  JsonObject &reply = jb.createObject();
  reply["cmd"] = "metrics_info";
  reply["esp_free_heap"] = ESP.getFreeHeap();
  reply["millis"] = millis();
  reply["net_rx_buf_max"] = net.rx_buffer_high_watermark;
  reply["net_tcp_double_connect_errors"] = net.tcp_double_connect_errors;
  reply["net_tcp_reconns"] = net.tcp_connects;
  reply["net_tcp_fingerprint_errors"] = net.tcp_fingerprint_errors;
  reply["net_tcp_async_errors"] = net.tcp_async_errors;
  reply["net_tcp_sync_errors"] = net.tcp_sync_errors;
  reply["net_tx_buf_max"] = net.tx_buffer_high_watermark;
  reply["net_tx_delay_count"] = net.tx_delay_count;
  reply["net_wifi_reconns"] = net.wifi_reconnections;
  reply["nfc_reset_count"] = nfc.reset_count;
  net.send_json(reply);
}

void network_cmd_ping(JsonObject &obj)
{
  DynamicJsonBuffer jb;
  JsonObject &reply = jb.createObject();
  reply["cmd"] = "pong";
  if (obj["seq"]) {
    reply["seq"] = obj["seq"];
  }
  if (obj["timestamp"]) {
    reply["timestamp"] = obj["timestamp"];
  }
  net.send_json(reply);
}

void network_cmd_reset(JsonObject &obj)
{
  reset_pending = true;
  if (obj["force"]) {
    led.off();
    ESP.reset();
    delay(5000);
  }
}

void network_cmd_restart(JsonObject &obj)
{
  restart_pending = true;
  if (obj["force"]) {
    led.off();
    ESP.restart();
    delay(5000);
  }
}

void network_cmd_state_query(JsonObject &obj)
{
  send_state();
}

void network_cmd_state_set(JsonObject &obj)
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

void network_cmd_system_query(JsonObject &obj)
{
  DynamicJsonBuffer jb;
  JsonObject &reply = jb.createObject();
  reply["cmd"] = "system_info";
  reply["esp_free_heap"] = ESP.getFreeHeap();
  reply["esp_chip_id"] = ESP.getChipId();
  reply["esp_sdk_version"] = ESP.getSdkVersion();
  reply["esp_core_version"] = ESP.getCoreVersion();
  reply["esp_boot_version"] = ESP.getBootVersion();
  reply["esp_boot_mode"] = ESP.getBootMode();
  reply["esp_cpu_freq_mhz"] = ESP.getCpuFreqMHz();
  reply["esp_flash_chip_id"] = ESP.getFlashChipId();
  reply["esp_flash_chip_real_size"] = ESP.getFlashChipRealSize();
  reply["esp_flash_chip_size"] = ESP.getFlashChipSize();
  reply["esp_flash_chip_speed"] = ESP.getFlashChipSpeed();
  reply["esp_flash_chip_mode"] = ESP.getFlashChipMode();
  reply["esp_flash_chip_size_by_chip_id"] = ESP.getFlashChipSizeByChipId();
  reply["esp_sketch_size"] = ESP.getSketchSize();
  reply["esp_sketch_md5"] = ESP.getSketchMD5();
  reply["esp_free_sketch_space"] = ESP.getFreeSketchSpace();
  reply["esp_reset_reason"] = ESP.getResetReason();
  reply["esp_reset_info"] = ESP.getResetInfo();
  reply["esp_cycle_count"] = ESP.getCycleCount();
  reply["millis"] = millis();
  net.send_json(reply);
}

void network_cmd_token_info(JsonObject &obj)
{
  token_info_callback(obj["uid"], obj["found"], obj["name"], obj["access"]);
}

void network_message_callback(JsonObject &obj)
{
  String cmd = obj["cmd"];

  last_network_activity = millis();

  if (cmd == "buzzer_beep") {
    network_cmd_buzzer_beep(obj);
  } else if (cmd == "buzzer_chirp") {
    network_cmd_buzzer_chirp(obj);
  } else if (cmd == "buzzer_click") {
    network_cmd_buzzer_click(obj);
  } else if (cmd == "buzzer_tune") {
    network_cmd_buzzer_tune(obj);
  } else if (cmd == "file_data") {
    network_cmd_file_data(obj);
  } else if (cmd == "file_delete") {
    network_cmd_file_delete(obj);
  } else if (cmd == "file_dir_query") {
    network_cmd_file_dir_query(obj);
  } else if (cmd == "file_query") {
    network_cmd_file_query(obj);
  } else if (cmd == "file_rename") {
    network_cmd_file_rename(obj);
  } else if (cmd == "file_write") {
    network_cmd_file_write(obj);
  } else if (cmd == "firmware_data") {
    network_cmd_firmware_data(obj);
  } else if (cmd == "firmware_write") {
    network_cmd_firmware_write(obj);
  } else if (cmd == "keepalive") {
    // ignore
  } else if (cmd == "metrics_query") {
    network_cmd_metrics_query(obj);
  } else if (cmd == "ping") {
    network_cmd_ping(obj);
  } else if (cmd == "pong") {
    // ignore
  } else if (cmd == "ready") {
    // ignore
  } else if (cmd == "reset") {
    network_cmd_reset(obj);
  } else if (cmd == "restart") {
    network_cmd_restart(obj);
  } else if (cmd == "state_query") {
    network_cmd_state_query(obj);
  } else if (cmd == "state_set") {
    network_cmd_state_set(obj);
  } else if (cmd == "system_query") {
    network_cmd_system_query(obj);
  } else if (cmd == "token_info") {
    network_cmd_token_info(obj);
  } else {
    DynamicJsonBuffer jb;
    JsonObject &reply = jb.createObject();
    reply["cmd"] = "error";
    reply["requested_cmd"] = reply["cmd"];
    reply["error"] = "not implemented";
    net.send_json(reply);
  }
}

void setup()
{
  pinMode(buzzer_pin, OUTPUT);
  pinMode(pn532_reset_pin, OUTPUT);

  digitalWrite(buzzer_pin, LOW);
  digitalWrite(pn532_reset_pin, HIGH);

  snprintf(clientid, sizeof(clientid), "doorman-%06x", ESP.getChipId());
  WiFi.hostname(String(clientid));

  Serial.begin(115200);
  for (int i=0; i<1024; i++) {
    Serial.print(" \b");
  }
  Serial.println();

  Serial.print(clientid);
  Serial.print(" ");
  Serial.println(ESP.getSketchMD5());

  Wire.begin(sda_pin, scl_pin);
  SPIFFS.begin();

  digitalWrite(prog_pin, HIGH);
  unsigned long start_time = millis();
  while (millis() - start_time < 500) {
    if (digitalRead(prog_pin) == LOW) {
      Serial.println("prog button pressed, going into setup mode");
      SetupMode setup_mode(clientid, setup_password);
      setup_mode.run();
      ESP.restart();
    }
  }

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

  net.state_callback = network_state_callback;
  net.message_callback = network_message_callback;
  net.begin(clientid);

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
  unsigned long loop_start_time;
  unsigned long loop_run_time;

  unsigned long start_time;
  long t_display, t_nfc, t_inputs, t_adc;

  loop_start_time = millis();

  yield();
  
  start_time = millis();
  nfc.loop();
  t_nfc = millis() - start_time;
  
  yield();
  
  start_time = millis();
  inputs.loop();
  t_inputs = millis() - start_time;
  
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

  loop_run_time = millis() - loop_start_time;
  if (loop_run_time > 56) {
    Serial.print("loop time "); Serial.print(loop_run_time, DEC); Serial.print("ms: ");
    Serial.print("nfc="); Serial.print(t_nfc, DEC); Serial.print("ms ");
    Serial.print("inputs="); Serial.print(t_inputs, DEC); Serial.print("ms ");
    Serial.println();
  }

  yield();

  if (config.network_watchdog_time != 0) {
    if (millis() - last_network_activity > config.network_watchdog_time) {
      if (!restart_pending) {
        Serial.println("network watchdog triggered, will restart when possible");
        restart_pending = true;
      }
    }
  }

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

  yield();

}

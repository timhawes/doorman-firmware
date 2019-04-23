#include "AppConfig.hpp"
#include <ArduinoJson.h>
#include <FS.h>
#include "app_util.h"

AppConfig::AppConfig() {
  LoadDefaults();
  LoadOverrides();
}

void AppConfig::LoadDefaults() {
  strlcpy(server_host, "", sizeof(server_host));
  strlcpy(server_password, "", sizeof(server_password));
  strlcpy(ssid, "", sizeof(ssid));
  strlcpy(wpa_password, "", sizeof(wpa_password));
  allow_snib_on_battery = false;
  card_unlock_time = 5000;
  error_sounds = true;
  exit_anti_bounce = false;
  exit_unlock_time = 5000;
  exit_unlock_time_max = 5000;
  hold_exit_for_snib = false;
  invert_relay = false;
  long_press_time = 1000;
  network_watchdog_time = 3600000;
  nfc_read_counter = false;
  nfc_read_data = 0;
  nfc_read_sig = false;
  remote_unlock_time = 86400000;
  server_port = 14260;
  server_tls_enabled = false;
  server_tls_verify = false;
  snib_unlock_time = 1800000;
  token_query_timeout = 1000;
  voltage_check_interval = 5000;
  voltage_falling_threshold = 13.7;
  voltage_multiplier = 1.470588;
  voltage_rising_threshold = 13.6;
}

void AppConfig::LoadOverrides() {

}

bool AppConfig::LoadJson(const char *filename) {
  File file = SPIFFS.open(filename, "r");
  if (!file) {
    Serial.println("AppConfig: file not found");
    LoadOverrides();
    return false;
  }

  DynamicJsonBuffer jb;
  JsonObject &root = jb.parseObject(file);
  file.close();

  if (!root.success()) {
    Serial.println("AppConfig: failed to parse JSON");
    LoadOverrides();
    return false;
  }

  root["server_host"].as<String>().toCharArray(server_host, sizeof(server_host));
  root["server_password"].as<String>().toCharArray(server_password, sizeof(server_password));
  root["ssid"].as<String>().toCharArray(ssid, sizeof(ssid));
  root["wpa_password"].as<String>().toCharArray(wpa_password, sizeof(wpa_password));

  allow_snib_on_battery = root["allow_snib_on_battery"];
  card_unlock_time = root["card_unlock_time"];
  error_sounds = root["error_sounds"];
  exit_anti_bounce = root["exit_anti_bounce"];
  exit_unlock_time = root["exit_unlock_time"];
  exit_unlock_time_max = root["exit_unlock_time_max"];
  hold_exit_for_snib = root["hold_exit_for_snib"];
  invert_relay = root["invert_relay"];
  long_press_time = root["long_press_time"];
  network_watchdog_time = root["network_watchdog_time"];
  nfc_read_counter = root["nfc_read_counter"];
  nfc_read_data = root["nfc_read_data"];
  nfc_read_sig = root["nfc_read_sig"];
  remote_unlock_time = root["remote_unlock_time"];
  server_port = root["server_port"];
  server_tls_enabled = root["server_tls_enabled"];
  server_tls_verify = root["server_tls_verify"];
  snib_unlock_time = root["snib_unlock_time"];
  token_query_timeout = root["token_query_timeout"];
  voltage_check_interval = root["voltage_check_interval"];
  voltage_falling_threshold = root["voltage_falling_threshold"];
  voltage_multiplier = root["voltage_multiplier"];
  voltage_rising_threshold = root["voltage_rising_threshold"];

  memset(server_fingerprint1, 0, sizeof(server_fingerprint1));
  memset(server_fingerprint2, 0, sizeof(server_fingerprint2));
  decode_hex(root["server_fingerprint1"].as<String>().c_str(),
             server_fingerprint1, sizeof(server_fingerprint1));
  decode_hex(root["server_fingerprint2"].as<String>().c_str(),
             server_fingerprint2, sizeof(server_fingerprint2));

  LoadOverrides();

  Serial.println("AppConfig: loaded");
  return true;
}

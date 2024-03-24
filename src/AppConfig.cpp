// SPDX-FileCopyrightText: 2019-2024 Tim Hawes
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AppConfig.hpp"
#include <ArduinoJson.h>
#include <FS.h>
#include "app_util.h"

AppConfig::AppConfig() {
  LoadDefaults();
  LoadOverrides();
}

void AppConfig::LoadDefaults() {
  // wifi
  strlcpy(ssid, "", sizeof(ssid));
  strlcpy(wpa_password, "", sizeof(wpa_password));
  wifi_check_interval = 60000;
  // net
  strlcpy(server_host, "", sizeof(server_host));
  strlcpy(server_password, "", sizeof(server_password));
  network_conn_stable_time = 30000;
  network_reconnect_max_time = 300000;
  network_watchdog_time = 3600000;
  server_port = 14260;
  server_tls_enabled = false;
  server_tls_verify = false;
  // app
  allow_snib_on_battery = false;
  anti_bounce = false;
  card_unlock_time = 5000;
  dev = false;
  events = false;
  exit_interactive_time = 0;
  exit_unlock_time = 5000;
  hold_exit_for_snib = false;
  invert_relay = false;
  led_bright = 1023;
  led_dim = 150;
  long_press_time = 1000;
  nfc_1m_limit = 60;
  nfc_5s_limit = 30;
  nfc_check_interval = 10000;
  nfc_read_counter = false;
  nfc_read_data = 0;
  nfc_read_sig = false;
  nfc_reset_interval = 1000;
  remote_unlock_time = 86400000;
  snib_unlock_time = 1800000;
  token_query_timeout = 1000;
  voltage_check_interval = 5000;
  voltage_falling_threshold = 13.7;
  voltage_multiplier = 0.0146;
  voltage_rising_threshold = 13.6;
}

void AppConfig::LoadOverrides() {

}

bool AppConfig::LoadWifiJson(const char *filename) {
  File file = SPIFFS.open(filename, "r");
  if (!file) {
    Serial.println("AppConfig: wifi file not found");
    LoadOverrides();
    return false;
  }

  DynamicJsonDocument root(4096);
  DeserializationError err = deserializeJson(root, file);
  file.close();

  if (err) {
    Serial.print("AppConfig: failed to parse JSON: ");
    Serial.println(err.c_str());
    LoadOverrides();
    return false;
  }

  root["ssid"].as<String>().toCharArray(ssid, sizeof(ssid));
  root["password"].as<String>().toCharArray(wpa_password, sizeof(wpa_password));
  wifi_check_interval = root["wifi_check_interval"] | 60000;

  LoadOverrides();

  Serial.println("AppConfig: wifi loaded");
  return true;
}

bool AppConfig::LoadNetJson(const char *filename) {
  File file = SPIFFS.open(filename, "r");
  if (!file) {
    Serial.println("AppConfig: net file not found");
    LoadOverrides();
    return false;
  }

  DynamicJsonDocument root(4096);
  DeserializationError err = deserializeJson(root, file);
  file.close();

  if (err) {
    Serial.print("AppConfig: failed to parse JSON: ");
    Serial.println(err.c_str());
    LoadOverrides();
    return false;
  }

  root["host"].as<String>().toCharArray(server_host, sizeof(server_host));
  root["password"].as<String>().toCharArray(server_password, sizeof(server_password));

  network_conn_stable_time = root["conn_stable_time"] | 30000;
  network_reconnect_max_time = root["reconnect_max_time"] | 300000;
  network_watchdog_time = root["watchdog_time"] | 3600000;
  server_port = root["port"] | 14260;
  server_tls_enabled = root["tls"] | false;
  server_tls_verify = root["tls_verify"] | false;

  memset(server_fingerprint1, 0, sizeof(server_fingerprint1));
  memset(server_fingerprint2, 0, sizeof(server_fingerprint2));
  decode_hex(root["tls_fingerprint1"].as<String>().c_str(),
             server_fingerprint1, sizeof(server_fingerprint1));
  decode_hex(root["tls_fingerprint2"].as<String>().c_str(),
             server_fingerprint2, sizeof(server_fingerprint2));

  LoadOverrides();

  Serial.println("AppConfig: net loaded");
  return true;
}

bool AppConfig::LoadAppJson(const char *filename) {
  File file = SPIFFS.open(filename, "r");
  if (!file) {
    Serial.println("AppConfig: app file not found");
    LoadOverrides();
    return false;
  }

  DynamicJsonDocument root(4096);
  DeserializationError err = deserializeJson(root, file);
  file.close();

  if (err) {
    Serial.print("AppConfig: failed to parse JSON: ");
    Serial.println(err.c_str());
    LoadOverrides();
    return false;
  }

  allow_snib_on_battery = root["allow_snib_on_battery"] | false;
  anti_bounce = root["anti_bounce"] | false;
  card_unlock_time = root["card_unlock_time"] | 5000;
  dev = root["dev"] | false;
  events = root["events"] | true;
  exit_interactive_time = root["exit_interactive_time"] | 0;
  exit_unlock_time = root["exit_unlock_time"] | 5000;
  hold_exit_for_snib = root["hold_exit_for_snib"] | false;
  invert_relay = root["invert_relay"] | false;
  led_bright = root["led_bright"] | 1023;
  led_dim = root["led_dim"] | 150;
  long_press_time = root["long_press_time"] | 1000;
  nfc_1m_limit = root["nfc_1m_limit"] | 60;
  nfc_5s_limit = root["nfc_5s_limit"] | 30;
  nfc_check_interval = root["nfc_check_interval"] | 10000;
  nfc_read_counter = root["nfc_read_counter"] | false;
  nfc_read_data = root["nfc_read_data"] | 0;
  nfc_read_sig = root["nfc_read_sig"] | false;
  nfc_reset_interval = root["nfc_reset_interval"] | 1000;
  remote_unlock_time = root["remote_unlock_time"] | 86400000;
  snib_unlock_time = root["snib_unlock_time"] | 1800000;
  token_query_timeout = root["token_query_timeout"] | 1000;
  voltage_check_interval = root["voltage_check_interval"] | 5000;
  voltage_falling_threshold = root["voltage_falling_threshold"] | 13.7;
  voltage_multiplier = root["voltage_multiplier"] | 0.0146;
  voltage_rising_threshold = root["voltage_rising_threshold"] | 13.6;

  LoadOverrides();

  Serial.println("AppConfig: app loaded");
  return true;
}

#ifndef APPCONFIG_HPP
#define APPCONFIG_HPP

#include <Arduino.h>

class AppConfig {
 public:
  AppConfig();
  bool allow_snib_on_battery;
  bool error_sounds;
  bool exit_anti_bounce;
  bool hold_exit_for_snib;
  bool invert_relay; // false=fail-secure, true=fail-safe/maglocks
  bool nfc_read_counter;
  bool nfc_read_sig;
  bool server_tls_enabled;
  bool server_tls_verify;
  char server_host[64];
  char server_password[64];
  char ssid[33];
  char wpa_password[64];
  float voltage_falling_threshold;
  float voltage_multiplier;
  float voltage_rising_threshold;
  int card_unlock_time;
  int exit_unlock_time_max;
  int exit_unlock_time;
  int long_press_time;
  int network_watchdog_time;
  int nfc_read_data;
  int remote_unlock_time;
  int server_port;
  int snib_unlock_time;
  int voltage_check_interval;
  long token_query_timeout;
  uint8_t server_fingerprint1[21];
  uint8_t server_fingerprint2[21];
  void LoadDefaults();
  bool LoadJson(const char *filename = "config.json");
  void LoadOverrides();
};

#endif

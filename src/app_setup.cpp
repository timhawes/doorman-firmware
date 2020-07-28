#include "app_setup.h"
#include <FS.h>
#include <ArduinoJson.h>

static const char html[] PROGMEM =
    "<form method='POST' action='/update' />\n"
    "<table>\n"
    "<tr><th>Key</th><th>Old Value</th><th>New Value</th></tr>\n"
    "<tr><th>SSID</th><td><input type='text' name='ssid' /></td></tr>\n"
    "<tr><th>WPA Password</th><td><input type='text' name='wpa_password' /></td></tr>\n"
    "<tr><th>Server Host</th><td><input type='text' name='server_host' /></td></tr>\n"
    "<tr><th>Server Port</th><td><input type='text' name='server_port' /></td></tr>\n"
    "<tr><th>Server TLS</th><td><input type='checkbox' name='server_tls_enabled' /></td></tr>\n"
    "<tr><th>Server Password</th><td><input type='text' name='server_password' /></td></tr>\n"
    "</table>\n"
    "<input type='submit' value='Save and Restart' />"
    "</form>\n";

SetupMode::SetupMode(const char *clientid, const char *setup_password) {
  _clientid = clientid;
  _setup_password = setup_password;
}

void SetupMode::configRootHandler() {
  String output = FPSTR(html);
  server.send(200, "text/html", output);
}

void SetupMode::configUpdateHandler() {
  DynamicJsonDocument root(4096);
  for (int i=0; i<server.args(); i++) {
    if (server.argName(i) == "ssid") root["ssid"] = server.arg(i);
    if (server.argName(i) == "wpa_password") root["wpa_password"] = server.arg(i);
    if (server.argName(i) == "server_host") root["server_host"] = server.arg(i);
    if (server.argName(i) == "server_port") root["server_port"] = server.arg(i).toInt();
    if (server.argName(i) == "server_tls") root["server_tls"] = (bool)server.arg(i).toInt();
    if (server.argName(i) == "server_password") root["server_password"] = server.arg(i);
  }
  File file = SPIFFS.open("config.json", "w");
  serializeJson(root, file);
  file.close();
  server.sendHeader("Location", "/");
  server.send(301);
  delay(500);
  ESP.restart();
}

void SetupMode::run() {
  Serial.println("in setup mode now");

  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(_clientid, _setup_password);

  delay(100);
  IPAddress ip = WiFi.softAPIP();

  // display access details
  Serial.print("WiFi AP: SSID=");
  Serial.print(_clientid);
  Serial.print(" URL=http://");
  Serial.print(ip);
  Serial.println("/");

  server.on("/", std::bind(&SetupMode::configRootHandler, this));
  server.on("/update", std::bind(&SetupMode::configUpdateHandler, this));
  server.begin(80);

  while (1) {
    server.handleClient();
  }

  Serial.println("leaving setup mode, restart");
  delay(500);
  ESP.restart();
}

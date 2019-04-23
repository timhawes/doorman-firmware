#ifndef APP_SETUP_H
#define APP_SETUP_H

#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>

class SetupMode
{
private:
  const char *_clientid;
  const char *_setup_password;
  ESP8266WebServer server;
  void configRootHandler();
  void configUpdateHandler();
public:
  SetupMode(const char *clientid, const char *setup_password);
  void run();
};

#endif

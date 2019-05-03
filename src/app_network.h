#ifndef APP_NETWORK_H
#define APP_NETWORK_H

#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <cbuf.h>

#include <ArduinoJson.h>
#include <ESPAsyncTCP.h>

typedef void (*network_message_callback_t)(JsonObject &obj);
typedef void (*network_state_callback_t)(bool wifi_up, bool tcp_up, bool ready);

class Network {
 private:
  AsyncClient *client;
  Ticker tcpReconnectTimer;
  Ticker wifiReconnectTimer;
  WiFiEventHandler wifiConnectHandler;
  WiFiEventHandler wifiDisconnectHandler;
  cbuf *rx_buffer;
  cbuf *tx_buffer;
  const char *clientid;
  bool debug_packet = false;
  bool debug_json = false;
  bool network_stopped = false;
  const uint8_t *server_fingerprint1;
  const uint8_t *server_fingerprint2;
  const char *server_host;
  const char *server_password;
  int server_port;
  bool server_tls_enabled;
  bool server_tls_verify;
  bool tcp_active = false;
  const char *wifi_password;
  const char *wifi_ssid;
  bool rx_scheduled = false;
  void connectToTcp();
  void connectToWifi();
  void onWifiConnect();
  void onWifiDisconnect();
  size_t process_rx_buffer();
  size_t process_tx_buffer();
  void receive_json(JsonObject &obj);
  void receive_packet(const uint8_t *data, int len);
  void send_cmd_hello();
  void send_packet(const uint8_t *data, int len, bool now = false);

 public:
  Network();
  int client_reconnections = 0;
  int rx_buffer_high_watermark = 0;
  int tx_buffer_high_watermark = 0;
  int tx_delay_count = 0;
  int wifi_reconnections = 0;
  network_message_callback_t message_callback = NULL;
  network_state_callback_t state_callback = NULL;
  void begin(const char *clientid);
  void send_json(JsonObject &obj, bool now = false);
  void set_server(const char *host, int port, const char *password,
                  bool tls_enabled, bool tls_verify = false,
                  const uint8_t *fingerprint1 = NULL,
                  const uint8_t *fingerprint2 = NULL);
  void set_wifi(const char *ssid, const char *password);
  void stop();
  void reconnect();
};

#endif

#include "app_network.h"

#include "app_util.h"
#include "Schedule.h"

Network::Network() {
  client = new AsyncClient();
  rx_buffer = new cbuf(1460);
  tx_buffer = new cbuf(1460);
}

void Network::set_wifi(const char *ssid, const char *password) {
  wifi_ssid = ssid;
  wifi_password = password;
}

void Network::set_server(const char *host, int port, const char *password,
                         bool tls_enabled,
                         bool tls_verify, const uint8_t *fingerprint1,
                         const uint8_t *fingerprint2) {
  server_host = host;
  server_port = port;
  server_tls_enabled = tls_enabled;
  server_tls_verify = tls_verify;
  server_password = password;
  server_fingerprint1 = fingerprint1;
  server_fingerprint2 = fingerprint2;
}

void Network::begin(const char *_clientid) {
  clientid = _clientid;

  wifiConnectHandler = WiFi.onStationModeGotIP(std::bind(&Network::onWifiConnect, this));
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(std::bind(&Network::onWifiDisconnect, this));

  if (state_callback) {
    state_callback(false, false, false);
  }

  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(true);
  WiFi.enableAP(false);
  WiFi.enableSTA(true);

  connectToWifi();
}

void Network::onWifiConnect() {
  Serial.print("network: wifi connected ");
  Serial.println(WiFi.localIP());

  wifi_reconnections++;

  if (state_callback) {
    state_callback(true, false, false);
  }

  tcpReconnectTimer.once_ms_scheduled(1, std::bind(&Network::connectToTcp, this));
}

void Network::onWifiDisconnect() {
  Serial.println("network: wifi disconnected");

  if (state_callback) {
    state_callback(false, false, false);
  }

  tcpReconnectTimer.detach();
  client->close(true);
}

void Network::connectToWifi() {
  if (network_stopped) {
    return;
  }

  String ssid = wifi_ssid;
  String password = wifi_password;

  if (WiFi.isConnected()) {
    if (WiFi.SSID() == ssid) {
      Serial.println("network: already connected to correct SSID");
      onWifiConnect();
      return;
    }
  }

  if (ssid.length() > 0) {
    Serial.println("network: connecting to WiFi...");
    if (password.length() > 0) {
      WiFi.begin(ssid.c_str(), password.c_str());
    } else {
      WiFi.begin(ssid.c_str());
    }
  } else {
    Serial.println("network: connecting to WiFi without SSID/password");
    WiFi.begin();
  }
}

void Network::connectToTcp() {
  if (network_stopped) {
    return;
  }

  if (tcp_active) {
    return;
  }

  tcp_active = true;

  client->onError(
      [=](void *arg, AsyncClient *c, int error) {
        Serial.print("network: TCP client error ");
        Serial.print(error, DEC);
        Serial.print(": ");
        Serial.println(c->errorToString(error));
        tcp_active = false;
        tcpReconnectTimer.once_scheduled(2, std::bind(&Network::connectToTcp, this));
      },
      NULL);

  client->onPoll(
      [=](void *arg, AsyncClient *c) {
        if (tx_buffer->available() > 0) {
          process_tx_buffer();
        }
      },
      NULL);

  client->onConnect(
      [=](void *arg, AsyncClient *c) {
        Serial.println("network: TCP client connected");
        client_reconnections++;
        if (server_tls_enabled && server_tls_verify) {
          SSL *ssl = c->getSSL();
          bool matched = false;
          if (ssl_match_fingerprint(ssl, server_fingerprint1) == 0) {
            Serial.println("network: TLS fingerprint #1 matched");
            matched = true;
          }
          if (ssl_match_fingerprint(ssl, server_fingerprint2) == 0) {
            Serial.println("network: TLS fingerprint #2 matched");
            matched = true;
          }
          if (!matched) {
            Serial.println("network: TLS fingerprint doesn't match");
            c->close(true);
          }
        } else {
          Serial.println("network: TLS fingerprint not verified");
        }
        rx_buffer->flush();
        tx_buffer->flush();
        Serial.println("network: TCP connected");
        if (state_callback) {
          state_callback(true, true, false);
        }
        send_cmd_hello();
      },
      NULL);

  client->onDisconnect(
      [=](void *arg, AsyncClient *c) {
        Serial.println("network: TCP client disconnected");
        rx_buffer->flush();
        tx_buffer->flush();
        if (state_callback) {
          state_callback(true, false, false);
        }
        tcp_active = false;
        tcpReconnectTimer.once_scheduled(2, std::bind(&Network::connectToTcp, this));
      },
      NULL);

  client->onData(
      [=](void *arg, AsyncClient *c, void *data, size_t len) {
        if (len > 0) {
          for (unsigned int i = 0; i < len; i++) {
            if (rx_buffer->write(((uint8_t *)data)[i]) != 1) {
              Serial.println("network: buffer is full, closing session!");
              DynamicJsonBuffer jb;
              JsonObject &obj = jb.createObject();
              obj["cmd"] = "error";
              obj["error"] = "receive buffer full";
              send_json(obj);
              c->close(true);
              return;
            }
          }
        }
        if (rx_buffer->available() > rx_buffer_high_watermark) {
          rx_buffer_high_watermark = rx_buffer->available();
        }
        // FIXME: Save memory by handling packets in-place and only use the
        // buffer for incomplete packets
        //process_rx_buffer();
        if (!rx_scheduled) {
          schedule_function(std::bind(&Network::process_rx_buffer, this));
          rx_scheduled = true;
          //rx_scheduled_time = millis();
        }
      },
      NULL);

  client->setAckTimeout(5000);
  client->setRxTimeout(300);
  client->setNoDelay(true);

  Serial.println("network: TCP client connecting...");
  if (!client->connect(server_host, server_port, server_tls_enabled)) {
    Serial.println("network: TCP client connect failed immediately");
    client->close(true);
    tcp_active = false;
    tcpReconnectTimer.once_scheduled(2, std::bind(&Network::connectToTcp, this));
  }
}

void Network::stop() {
  network_stopped = true;
  wifiReconnectTimer.detach();
  tcpReconnectTimer.detach();
  client->close(true);
  WiFi.disconnect();
}

void Network::receive_packet(const uint8_t *packet, int len) {
  if (debug_packet) {
    Serial.print("network: recv ");
    String hex = hexlify((uint8_t*)packet, len);
    Serial.println(hex);
  }

  DynamicJsonBuffer jb;
  JsonObject &obj = jb.parseObject(packet);
  receive_json(obj);
}

void Network::send_packet(const uint8_t *data, int len, bool now) {
  char header[3];

  if (debug_packet) {
    Serial.print("network: send ");
    String hex = hexlify((uint8_t*)data, len);
    Serial.println(hex);
  }

  header[0] = (len & 0xFF00) >> 8;
  header[1] = len & 0xFF;
  tx_buffer->write(header[0]);
  tx_buffer->write(header[1]);
  for (int i = 0; i < len; i++) {
    tx_buffer->write(data[i]);
  }

  if (now) {
    process_tx_buffer();
  }
}

void Network::receive_json(JsonObject &obj) {
  if (debug_json) {
    Serial.print("network: recv ");
    obj.printTo(Serial);
    Serial.println();
  }

  if (obj["cmd"] == "ready") {
    Serial.println("network: ready");
    if (state_callback) {
      state_callback(true, true, true);
    }
  }

  if (message_callback) {
    message_callback(obj);
  }
}

void Network::send_json(JsonObject &obj, bool now) {
  String json;
  obj.printTo(json);

  if (debug_json) {
    Serial.print("network: send ");
    obj.printTo(Serial);
    Serial.println();
  }

  send_packet((const uint8_t *)json.c_str(), json.length(), now);
}

void Network::send_cmd_hello() {
  DynamicJsonBuffer jb;
  JsonObject &obj = jb.createObject();
  obj["cmd"] = "hello";
  obj["clientid"] = clientid;
  obj["password"] = server_password;
  send_json(obj);
}

size_t Network::process_tx_buffer() {
  size_t available = tx_buffer->available();
  if (available > tx_buffer_high_watermark) {
    tx_buffer_high_watermark = available;
  }
  if (available > 0) {
    if (client->canSend()) {
      size_t sendable = client->space();
      if (sendable < available) {
        available = sendable;
      }
      char *out = new char[available];
      tx_buffer->read(out, available);
      size_t sent = client->write(out, available);
      delete[] out;
      return sent;
    } else {
      Serial.println("network: send_tx_buffer() can't send yet");
      tx_delay_count++;
    }
  }
  return 0;
}

size_t Network::process_rx_buffer() {
  rx_scheduled = false;

  int processed_bytes = 0;
  
  while (rx_buffer->available() >= 2) {
    // while (receive_buffer->getSize() >= 2) {
    // at least two bytes in the buffer
    char peekbuf[2];
    rx_buffer->peek(peekbuf, 2);
    uint16_t length = (peekbuf[0] << 8) | peekbuf[1];
    if (rx_buffer->available() >= length + 2) {
      uint8_t packet[length];
      rx_buffer->remove(2);
      rx_buffer->read((char *)packet, length);
      processed_bytes++;
      receive_packet(packet, length);
    } else {
      // packet isn't complete
      break;
    }
  };

  return processed_bytes;
}

void Network::reconnect() {
  client->close(true);
}

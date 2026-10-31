#include <Arduino.h>
#include <string.h>
#include <string>
#include "config.h"
#include "App.h"
#include "TK80Core.h"

#ifndef HOST
  // ================= Lógica para ESP32 =================
  #include <WiFi.h>
  #include <ESPmDNS.h>
  #include <LittleFS.h>
  #include <ESPAsyncWebServer.h>
  #include <ArduinoJson.h>
  // Modo STA (-DTK80_WIFI_STA, entorno esp32dev_sta): se conecta al SSID de include/secrets.h.
  // Sin esa opción el ESP32 crea su propio punto de acceso (AP).
  #ifdef TK80_WIFI_STA
    #include "secrets.h"   // WIFI_SSID / WIFI_PASSWORD (copiar de secrets_example.h)
  #endif

  // Se mantiene el servidor asíncrono porque la interfaz usa WebSocket.
  static AsyncWebServer server(80);
  static AsyncWebSocket ws("/ws");

  // El display se reenvía aunque no cambie; la interfaz web cierra un socket
  // que permanece mudo 12 s, así que esto debe quedar muy por debajo.
  static constexpr uint32_t DISPLAY_HEARTBEAT_MS = 3000;
  static uint32_t lastDisplayMs = 0;
  static uint32_t lastSendMs = 0;
  static uint32_t lastCleanupMs = 0;
  static bool hadClients = false;

  struct HexUploadContext {
    std::string content;
    bool tooLarge = false;
  };

  #ifdef TK80_WIFI_STA
  static uint32_t lastWiFiRetryMs = 0;
  static bool mdnsStarted = false;
  static volatile bool mdnsPending = false;

  static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    (void)info;
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.print("[TK80] Wi-Fi connected; IP: ");
        Serial.println(WiFi.localIP());
        mdnsPending = true;  // se arranca desde loop(), no desde la tarea de eventos
        break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        Serial.println("[TK80] Wi-Fi disconnected; retrying...");
        break;
      default:
        break;
    }
  }
  #endif

  static void sendDisplay(AsyncWebSocketClient* client) {
    char json[80];
    int n = app::displayJson(json, sizeof(json));
    if (n > 0 && client && client->status() == WS_CONNECTED && client->canSend())
      client->text(json, n);
  }

  static void onWsEvent(AsyncWebSocket* srv, AsyncWebSocketClient* client,
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
    (void)srv; (void)arg;
    if (type == WS_EVT_CONNECT) {
      sendDisplay(client);
      return;
    }
    if (type != WS_EVT_DATA || data == nullptr || len == 0) return;

    JsonDocument doc;
    if (deserializeJson(doc, data, len)) return;
    const char* key = doc["key"] | "";
    const bool pressed = doc["pressed"] | false;
    if (!key[0]) return;

    // El teclado es una matriz leída por la ROM: se transmiten pulsación y suelta.
    // RESET detiene la CPU mientras está pulsado y arranca desde 0000h al soltarlo.
    if (!tk80::setKey(key, pressed)) Serial.printf("[TK80] unknown key '%s'\n", key);
  }

  // La CPU 8080 corre en su propia tarea del core 1. Wi-Fi y AsyncTCP viven en
  // el core 0 (CONFIG_ASYNC_TCP_RUNNING_CORE en platformio.ini).
  static void emuTask(void*) {
    for (;;) {
      app::pump(micros());
      vTaskDelay(1);  // cede siempre: permite que corran la tarea idle y loop()
    }
  }
#else
  // ================= Lógica nativa (Linux) =================
  #include "HostHttp.h"
#endif

static void logLine(const char* msg) {
  Serial.print("[TK80] ");
  Serial.println(msg);
}

void setup() {
  Serial.begin(115200);
  delay(150);
  app::setLogger(logLine);
  app::init();

#ifndef HOST
  // ----- ESP32 -----
  if (!LittleFS.begin(true)) Serial.println("[TK80] LittleFS mount failed");
  xTaskCreatePinnedToCore(emuTask, "tk80-emu", 6144, nullptr, 1, nullptr, 1);

#ifdef TK80_WIFI_STA
  // Modo STA: conexión al SSID de secrets.h
  WiFi.onEvent(onWiFiEvent);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[TK80] Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);
  const uint32_t wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[TK80] Open http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[TK80] Wi-Fi not connected yet; background reconnection remains enabled.");
  }
#else
  // Modo punto de acceso: el ESP32 crea su propia red (sin router).
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  WiFi.setSleep(false);  // el ahorro de energía añade latencia al WebSocket
  Serial.print("[TK80] Conéctate al WiFi '" AP_SSID "' y entra en: http://");
  Serial.println(WiFi.softAPIP());
  if (MDNS.begin(MDNS_HOSTNAME)) MDNS.addService("http", "tcp", 80);
#endif

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(LittleFS, "/index.html", "text/html; charset=utf-8");
  });
  server.on("/tape/store", HTTP_POST, [](AsyncWebServerRequest* request) {
    std::string body;
    int status = app::tapeStore(body);
    request->send(status, "application/json", body.c_str());
  });
  server.on("/hex/upload", HTTP_POST,
    [](AsyncWebServerRequest* request) {
      auto* upload = static_cast<HexUploadContext*>(request->_tempObject);
      request->_tempObject = nullptr;
      if (!upload) {
        request->send(400, "application/json", "{\"ok\":false,\"error\":\"No se recibió el archivo\"}");
        return;
      }
      std::string body;
      int status = upload->tooLarge ? 413 : 0;
      if (upload->tooLarge) body = "{\"ok\":false,\"error\":\"Archivo demasiado grande (máximo 32 KiB)\"}";
      else status = app::submitHex(std::move(upload->content), body);
      delete upload;
      request->send(status, "application/json", body.c_str());
    },
    nullptr,
    [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        delete static_cast<HexUploadContext*>(request->_tempObject);
        request->_tempObject = new HexUploadContext();
      }
      auto* upload = static_cast<HexUploadContext*>(request->_tempObject);
      if (!upload) return;
      if (total > MAX_HEX_UPLOAD) { upload->tooLarge = true; return; }
      if (!upload->tooLarge && len) upload->content.append((const char*)data, len);
    });
  server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "Not found");
  });
  server.begin();
#else
  // ----- PC (Linux): no hay Wi-Fi, el servidor escucha en localhost -----
  if (hostHttpBegin(HOST_HTTP_PORT, "data")) {
    Serial.println("[TK80] Servidor emulado listo. Entra en: http://localhost:8080");
    Serial.print("[TK80] Interfaz web servida desde: ");
    Serial.println(hostHttpRoot());
  } else
    Serial.println("[TK80] No se pudo abrir el puerto HTTP");
#endif
}

void loop() {
#ifdef HOST
  // Sin FreeRTOS: la CPU y el servidor comparten hilo.
  app::pump(micros());
  hostHttpPoll();
  // Sin WebSocket no hay aviso de desconexión: si la interfaz deja de sondear
  // (pestaña cerrada) con una tecla pulsada, se sueltan todas las teclas.
  if (hostHttpIdleMs() > 2000) tk80::releaseAllKeys();
  delay(1);
#else
  // La CPU corre en emuTask(); loop() solo hace el mantenimiento de red.
  const uint32_t now = millis();

#ifdef TK80_WIFI_STA
  // mDNS tras cada asociación Wi-Fi y reintento acotado de conexión.
  if (mdnsPending && WiFi.status() == WL_CONNECTED) {
    mdnsPending = false;
    if (mdnsStarted) MDNS.end();
    mdnsStarted = MDNS.begin(MDNS_HOSTNAME);
    if (mdnsStarted) MDNS.addService("http", "tcp", 80);
  }
  if (WiFi.status() != WL_CONNECTED && now - lastWiFiRetryMs >= 10000) {
    lastWiFiRetryMs = now;
    Serial.println("[TK80] Wi-Fi retry...");
    WiFi.reconnect();
  }
#endif

  // Si se pierde el último cliente con una tecla pulsada, se sueltan todas
  // (si no, la matriz de teclado quedaría "pulsada" para siempre).
  if (ws.count()) hadClients = true;
  else if (hadClients) { hadClients = false; tk80::releaseAllKeys(); }

  // Difusión del display cuando cambia (máx. 10 Hz) y, como latido, cada
  // DISPLAY_HEARTBEAT_MS. Se omite si la cola de algún cliente está llena;
  // si no, AsyncWebSocket lo desconecta.
  static uint8_t lastBroadcast[8] = {0};
  static bool haveBroadcast = false;
  if (now - lastDisplayMs >= 100) {
    lastDisplayMs = now;
    const uint8_t* d = tk80::display();
    const bool changed = !haveBroadcast || memcmp(d, lastBroadcast, sizeof(lastBroadcast)) != 0;
    const bool heartbeatDue = (now - lastSendMs) >= DISPLAY_HEARTBEAT_MS;
    if ((changed || heartbeatDue) && ws.count() && ws.availableForWriteAll()) {
      char json[80];
      int n = app::displayJson(json, sizeof(json));
      if (n > 0) {
        ws.textAll(json);
        memcpy(lastBroadcast, d, sizeof(lastBroadcast));
        haveBroadcast = true;
        lastSendMs = now;
      }
    }
  }

  if (now - lastCleanupMs >= 1000) {
    lastCleanupMs = now;
    ws.cleanupClients();
  }

  delay(5);
#endif
}

#ifdef HOST
// Arduino-Emulator no enlaza un main() en este entorno: lo aportamos aquí.
// Es débil, así que si la librería llega a definir el suyo, prevalece el de ella.
__attribute__((weak)) int main() {
  setup();
  for (;;) loop();
}
#endif

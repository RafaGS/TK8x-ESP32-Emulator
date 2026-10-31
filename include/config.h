// Configuración común (ESP32 y PC).
#pragma once

// Reloj emulado de la CPU en kHz: el TK-80 original usa un uPD8080A a 2,048 MHz.
#ifndef TK80_CLOCK_KHZ
#define TK80_CLOCK_KHZ 2048
#endif

// Punto de acceso Wi-Fi que crea el ESP32 (la contraseña debe tener 8 caracteres o más).
#define AP_SSID      "TK80"
#define AP_PASSWORD  "tk80tk80"

// Nombre mDNS en el ESP32: http://tk80.local
#define MDNS_HOSTNAME "tk80"

// Puerto del servidor HTTP en el PC (-DHOST): http://localhost:8080
#define HOST_HTTP_PORT 8080

// Tamaño máximo de un archivo Intel HEX subido.
#define MAX_HEX_UPLOAD 32768

// STORE DATA: bytes máximos del rango [dirección, dato] que se guarda.
#define MAX_TAPE_BYTES 4096

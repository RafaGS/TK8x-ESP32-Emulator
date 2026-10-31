// Lógica común a ESP32 y PC: reparto de tiempo de la CPU, carga HEX y JSON del
// display. No depende de Arduino; el transporte (WebSocket, HTTP) vive en main.cpp.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace app {

void init();
void setLogger(void (*fn)(const char*));   // mensajes de diagnóstico (opcional)

// Una iteración de la emulación. nowUs = micros(). En el ESP32 la llama la tarea
// del core 1; en el PC, loop().
void pump(uint32_t nowUs);

// {"d":[b0,...,b7]}; devuelve la longitud escrita.
int displayJson(char* buf, size_t size);

// Valida el HEX y lo deja a la espera de que pump() lo aplique. Devuelve el
// código HTTP y deja en body el JSON de respuesta.
int submitHex(std::string&& content, std::string& body);

// STORE DATA: devuelve el rango [ADRES, DATA] del monitor como Intel HEX en JSON:
// {"ok":true,"start":"8100","end":"811C","bytes":29,"hex":"...\n..."}
// Devuelve el código HTTP.
int tapeStore(std::string& body);

}  // namespace app

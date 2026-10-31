// Servidor HTTP mínimo para el modo nativo (PC/Linux, -DHOST). Sin WebSocket:
// la interfaz web usa sondeo HTTP (/display, /key). Solo escucha en 127.0.0.1.
#pragma once
#ifdef HOST
#include <stdint.h>

// Abre el puerto y fija el directorio del que se sirve index.html (p. ej. "data").
bool hostHttpBegin(uint16_t port, const char* dataDir);
// Carpeta de la que se sirve index.html (tras la búsqueda en hostHttpBegin).
const char* hostHttpRoot();
// Milisegundos desde la última petición HTTP (0 si aún no ha llegado ninguna).
uint32_t hostHttpIdleMs();
// Atiende las conexiones pendientes sin bloquear; llamar desde loop().
void hostHttpPoll();
#endif

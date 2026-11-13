// Máquinas de la familia TK-80/TK-85: placa genérica dirigida por una descripción (Spec).
// Comparten 8255 en F8h-FBh, teclado por exploración (PC4-PC6), display por DMA
// (8 dígitos en RAM) y monitor con registros ADRES/DATA; cambian ROM, mapa, teclas,
// reloj y CPU.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include "I8080.h"

namespace machine {

// Tecla de la matriz: línea de exploración (0 = PC4, 1 = PC5, 2 = PC6) y bit leído en el puerto A.
// line = 0xFF: la tecla no está en la matriz (solo actúa por hardware).
// trap: la tecla dispara la interrupción TRAP del 8085 (TK-85: MON).
struct MatrixKey { const char* name; uint8_t line; uint8_t bit; bool trap = false; };

// Tecla de la interfaz. kind: 'h' dígito hexadecimal, 'f' función, 't' STORE/LOAD DATA
// (los gestiona la interfaz, no llegan a la ROM), 'r' RESET dentro de la rejilla, 'R' RESET como
// botón aparte (pulsador negro de la placa), ' ' hueco en la rejilla.
// label es el rótulo principal y sub el secundario (opcional). Por defecto sub se dibuja pequeño
// (<small>); con sameSize = true las dos líneas (sub encima, label debajo) tienen el mismo tamaño.
struct UiKey { char kind; const char* name; const char* label; const char* sub; bool sameSize = false; };

struct Spec {
  const char* id;               // "tk80", "tk85"
  const char* name;             // "NEC TK-80"
  CpuVariant cpu;
  uint32_t clockKHz;
  const uint8_t* rom; uint16_t romSize;       // ROM del monitor desde 0000h
  uint16_t progStart, progSize;               // zona de programa: la CPU solo la lee, el cargador la escribe
  uint16_t ramStart, ramSize;
  uint16_t dispAddr;                          // 8 dígitos; el 0 es el de la izquierda
  uint16_t monData, monAdres;                 // registros del monitor en RAM (16 bits little-endian)
  const MatrixKey* matrix; uint8_t nMatrix;
  bool tapeKeys;                              // la máquina tiene STORE DATA / LOAD DATA resueltas por la interfaz
  const UiKey* ui; uint8_t nUi; uint8_t uiCols;
};

constexpr size_t MAX_RAM  = 4096;
constexpr size_t MAX_PROG = 0x1800;

// ---- catálogo ----
size_t        count();
const Spec*   at(size_t i);
const Spec*   find(const char* id);                    // nullptr si no existe
std::string   describeJson(const Spec& spec);          // máquina + teclado + lista de máquinas, para la interfaz

// ---- máquina activa ----
bool          init(const char* id);                    // activa y reinicia; false si el id no existe
bool          request(const char* id);                 // cambio asíncrono, seguro entre hilos; lo aplica pollReset()
const Spec&   spec();                                  // máquina en marcha
const Spec&   target();                                // la pedida con request(), o la activa

int           step();                                  // una instrucción; 0 si RESET está pulsado
void          pollReset();                             // aplica un cambio de máquina o un RESET pendientes

uint8_t       read(uint16_t addr);
void          write(uint16_t addr, uint8_t value);     // ROM y zona de programa: solo lectura para la CPU
uint16_t      readWord(uint16_t addr);                 // little-endian
bool          isLoadable(uint16_t addr);               // RAM y zona de programa
void          loadByte(uint16_t addr, uint8_t value);  // cargador (Intel HEX)

const uint8_t* display();                              // 8 bytes de segmentos: bit0 = a ... bit6 = g, bit7 = punto
bool          setKey(const char* name, bool pressed);  // false si la tecla no existe en la máquina
void          releaseAllKeys();
uint16_t      cpuPC();
bool          lastOut(uint8_t& port, uint8_t& value);   // último OUT de la CPU (diagnóstico y pruebas); false si no hubo

}  // namespace machine

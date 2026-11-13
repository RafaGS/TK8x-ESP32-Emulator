// Placa genérica de la familia TK-80/TK-85 (ver Machine.h): memoria, 8255, teclado y RESET.
// Comportamiento tomado del emulador notwendig/tk80 y de las ROM originales.
#include "Machine.h"
#include "I8080.h"
#include <atomic>
#include <string.h>
#include <strings.h>

#if defined(ARDUINO)
#include <pgmspace.h>
#else
#define pgm_read_byte(a) (*(const uint8_t*)(a))
#endif

namespace machine {

static I8080   cpu;
static uint8_t ram[MAX_RAM];
static uint8_t prog[MAX_PROG];

// Máquina en marcha: solo la cambia la tarea de emulación (init/pollReset). Otras tareas
// pueden leerla (display, JSON de la interfaz): es un puntero a datos constantes.
static const Spec* S = nullptr;
static std::atomic<const Spec*> pendingSpec{nullptr};

// 8255 (puertos F8h-FBh)
static constexpr uint8_t PORT_A = 0xF8, PORT_B = 0xF9, PORT_C = 0xFA, PORT_CTRL = 0xFB;
static uint8_t pa, pb, pc;

// Matriz de teclado: tres líneas de exploración (PC4, PC5, PC6 a 0). Bit a 0 = tecla pulsada.
// Un único escritor (tarea de red), lector: emulación.
static volatile uint8_t keymap[3] = {0xFF, 0xFF, 0xFF};
static volatile bool resetHeld = false;
static volatile bool resetPending = false;
static volatile bool trapRequest = false;   // la pone la tarea de red (tecla MON) y la consume step()

// ---------------------------------------------------------------- bus

uint8_t read(uint16_t a) {
  const Spec& s = *S;
  if (a < s.romSize) return pgm_read_byte(s.rom + a);
  if (a >= s.progStart && a < (uint32_t)s.progStart + s.progSize) return prog[a - s.progStart];
  if (a >= s.ramStart && a < (uint32_t)s.ramStart + s.ramSize)    return ram[a - s.ramStart];
  return 0xFF;
}

void write(uint16_t a, uint8_t v) {
  const Spec& s = *S;
  if (a >= s.ramStart && a < (uint32_t)s.ramStart + s.ramSize) ram[a - s.ramStart] = v;
  // ROM y zona de programa: solo lectura para la CPU
}

bool isLoadable(uint16_t a) {
  const Spec& s = *S;
  return (a >= s.progStart && a < (uint32_t)s.progStart + s.progSize) ||
         (a >= s.ramStart && a < (uint32_t)s.ramStart + s.ramSize);
}

void loadByte(uint16_t a, uint8_t v) {
  const Spec& s = *S;
  if (a >= s.progStart && a < (uint32_t)s.progStart + s.progSize) prog[a - s.progStart] = v;
  else if (a >= s.ramStart && a < (uint32_t)s.ramStart + s.ramSize) ram[a - s.ramStart] = v;
}

uint16_t readWord(uint16_t a) { return (uint16_t)(read(a) | (read((uint16_t)(a + 1)) << 8)); }

const uint8_t* display() { return &ram[S->dispAddr - S->ramStart]; }

// ---------------------------------------------------------------- 8255

static uint8_t portIn(uint8_t port) {
  switch (port) {
    case PORT_A: {
      uint8_t x = 0xFF;
      if (!(pc & 0x10)) x &= keymap[0];
      if (!(pc & 0x20)) x &= keymap[1];
      if (!(pc & 0x40)) x &= keymap[2];
      return pa = x;
    }
    case PORT_B: return 0xFF;      // entrada serie sin dispositivo: línea en reposo = 1
    case PORT_C: return pc;
    default:     return 0xFF;
  }
}

static volatile bool    haveOut = false;
static volatile uint8_t lastOutPort = 0, lastOutValue = 0;

bool lastOut(uint8_t& port, uint8_t& value) { port = lastOutPort; value = lastOutValue; return haveOut; }

static void portOut(uint8_t port, uint8_t v) {
  if (port != PORT_C) { lastOutPort = port; lastOutValue = v; haveOut = true; }   // el monitor escribe PC sin parar (exploración)
  switch (port) {
    case PORT_A: pa = v; break;
    case PORT_B: pb = v; break;
    case PORT_C: pc = v; break;
    case PORT_CTRL:
      if (v & 0x80) { pa = pb = pc = 0; }                       // selección de modo: borra los latches
      else {                                                    // set/reset de un bit de C
        uint8_t bit = (v >> 1) & 7;
        if (v & 1) pc |= (1 << bit); else pc &= ~(1 << bit);
      }
      break;
    default: break;
  }
}

// ---------------------------------------------------------------- placa

static void resetBoard() {
  cpu.reset();
  cpu.sid = true;                // serie en reposo (solo la usa el 8085)
  pa = pb = pc = 0;
}

static void activate(const Spec* s) {
  S = s;
  memset(ram, 0xFF, sizeof(ram));
  memset(prog, 0xFF, sizeof(prog));
  keymap[0] = keymap[1] = keymap[2] = 0xFF;
  resetHeld = resetPending = trapRequest = false;
  cpu.rd = read; cpu.wr = write; cpu.in = portIn; cpu.out = portOut;
  cpu.variant = s->cpu;
  resetBoard();
}

bool init(const char* id) {
  const Spec* s = find(id);
  if (!s) return false;
  pendingSpec = nullptr;
  activate(s);
  return true;
}

bool request(const char* id) {
  const Spec* s = find(id);
  if (!s) return false;
  pendingSpec = s;
  return true;
}

const Spec& spec() { return *S; }
const Spec& target() { const Spec* p = pendingSpec; return p ? *p : *S; }

int step() {
  if (resetHeld) return 0;
  if (trapRequest) { trapRequest = false; cpu.pulseTrap(); }
  return cpu.step();
}

void pollReset() {
  if (const Spec* p = pendingSpec.exchange(nullptr)) { if (p != S) activate(p); }
  if (resetPending) { resetPending = false; resetBoard(); }
}

uint16_t cpuPC() { return cpu.pc; }

// ---------------------------------------------------------------- teclado

bool setKey(const char* name, bool pressed) {
  if (!strcasecmp(name, "reset")) {
    if (pressed) resetHeld = true;
    else if (resetHeld) { resetHeld = false; resetPending = true; }   // al soltar arranca desde 0000h
    return true;
  }
  const Spec& s = *S;
  // STORE DATA / LOAD DATA no se envían a la ROM (arrancaría su rutina de E/S serie, que aquí
  // no tiene dispositivo): se resuelven en la interfaz con app::tapeStore().
  if (s.tapeKeys && (!strcasecmp(name, "store") || !strcasecmp(name, "load"))) return true;
  for (uint8_t i = 0; i < s.nMatrix; ++i) {
    const MatrixKey& k = s.matrix[i];
    if (!strcasecmp(name, k.name)) {
      if (k.line < 3) {
        const uint8_t m = (uint8_t)(1u << k.bit), v = keymap[k.line];
        keymap[k.line] = pressed ? (uint8_t)(v & ~m) : (uint8_t)(v | m);
      }
      if (pressed && k.trap) trapRequest = true;
      return true;
    }
  }
  return false;
}

void releaseAllKeys() {
  keymap[0] = keymap[1] = keymap[2] = 0xFF;
  if (resetHeld) { resetHeld = false; resetPending = true; }
}

}  // namespace machine

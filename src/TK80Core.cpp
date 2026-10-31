#include "TK80Core.h"
#include "I8080.h"
#include "tk80rom.h"
#include <string.h>
#include <strings.h>

namespace tk80 {

static I8080   cpu;
static uint8_t ram[RAM_SIZE];
static uint8_t eep[EEP_SIZE];

// 8255 (puertos F8h-FBh)
static constexpr uint8_t PORT_A = 0xF8, PORT_B = 0xF9, PORT_C = 0xFA, PORT_CTRL = 0xFB;
static uint8_t pa, pb, pc;

// Matriz de teclado: tres líneas de exploración (PC4, PC5, PC6 a 0).
// Bit a 0 = tecla pulsada. Un único escritor (tarea de red), lector: emulación.
static volatile uint8_t keymap[3] = {0xFF, 0xFF, 0xFF};
static volatile bool resetHeld = false;
static volatile bool resetPending = false;

// ---------------------------------------------------------------- bus

uint8_t read(uint16_t a) {
  if (a < ROM_START + ROM_SIZE)                  return pgm_read_byte(&tk80rom[a]);
  if (a >= EEP_START && a < EEP_START + EEP_SIZE) return eep[a - EEP_START];
  if (a >= RAM_START && a < RAM_START + RAM_SIZE) return ram[a - RAM_START];
  return 0xFF;
}

void write(uint16_t a, uint8_t v) {
  if (a >= RAM_START && a < RAM_START + RAM_SIZE) ram[a - RAM_START] = v;
  // ROM y EEPROM: solo lectura para la CPU
}

bool isLoadable(uint16_t a) {
  return (a >= EEP_START && a < EEP_START + EEP_SIZE) ||
         (a >= RAM_START && a < RAM_START + RAM_SIZE);
}

void loadByte(uint16_t a, uint8_t v) {
  if (a >= EEP_START && a < EEP_START + EEP_SIZE) eep[a - EEP_START] = v;
  else if (a >= RAM_START && a < RAM_START + RAM_SIZE) ram[a - RAM_START] = v;
}

uint16_t readWord(uint16_t a) { return (uint16_t)(read(a) | (read((uint16_t)(a + 1)) << 8)); }

const uint8_t* display() { return &ram[DISP_ADDR - RAM_START]; }

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

static void portOut(uint8_t port, uint8_t v) {
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
  pa = pb = pc = 0;
}

void init() {
  memset(ram, 0xFF, sizeof(ram));
  memset(eep, 0xFF, sizeof(eep));
  keymap[0] = keymap[1] = keymap[2] = 0xFF;
  resetHeld = resetPending = false;
  cpu.rd = read; cpu.wr = write; cpu.in = portIn; cpu.out = portOut;
  resetBoard();
}

int step() {
  if (resetHeld) return 0;
  return cpu.step();
}

void pollReset() {
  if (resetPending) { resetPending = false; resetBoard(); }
}

uint16_t cpuPC() { return cpu.pc; }

// ---------------------------------------------------------------- teclado

struct KeyDef { const char* name; uint8_t line; uint8_t bit; };
static const KeyDef KEYS[] = {
  {"0",0,0},{"1",0,1},{"2",0,2},{"3",0,3},{"4",0,4},{"5",0,5},{"6",0,6},{"7",0,7},
  {"8",1,0},{"9",1,1},{"a",1,2},{"b",1,3},{"c",1,4},{"d",1,5},{"e",1,6},{"f",1,7},
  {"run",2,0},{"ret",2,1},{"adrs",2,2},{"rdec",2,3},{"rinc",2,4},{"winc",2,5},
};

bool setKey(const char* name, bool pressed) {
  if (!strcasecmp(name, "reset")) {
    if (pressed) resetHeld = true;
    else if (resetHeld) { resetHeld = false; resetPending = true; }   // al soltar arranca desde 0000h
    return true;
  }
  // STORE DATA / LOAD DATA no se envían a la ROM (arrancaría su rutina de E/S serie,
  // que aquí no tiene dispositivo): se resuelven en la interfaz con app::tapeStore().
  if (!strcasecmp(name, "store") || !strcasecmp(name, "load")) return true;
  for (const KeyDef& k : KEYS) {
    if (!strcasecmp(name, k.name)) {
      uint8_t m = 1u << k.bit, v = keymap[k.line];
      keymap[k.line] = pressed ? (v & ~m) : (v | m);
      return true;
    }
  }
  return false;
}

void releaseAllKeys() {
  keymap[0] = keymap[1] = keymap[2] = 0xFF;
  if (resetHeld) { resetHeld = false; resetPending = true; }
}

}  // namespace tk80

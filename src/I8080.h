// Núcleo Intel 8080 / NEC uPD8080A, independiente de Arduino (se puede
// compilar y probar en el PC). Contabiliza estados T por instrucción.
#pragma once
#include <stdint.h>

// Variante de CPU: el 8085 es un 8080 con RIM/SIM, E/S serie (SID/SOD), cuatro
// interrupciones propias y tiempos de instrucción distintos.
enum class CpuVariant : uint8_t { I8080, I8085 };

struct I8080 {
  // Registros: r[0..7] = B,C,D,E,H,L,(M no usado),A
  uint8_t  r[8];
  uint16_t sp, pc;
  uint8_t  f;          // S Z 0 AC 0 P 1 C
  bool     inte;       // flip-flop de interrupciones
  bool     halted;

  CpuVariant variant = CpuVariant::I8080;
  // Solo 8085: máscaras (bit0 = 5.5, bit1 = 6.5, bit2 = 7.5; 1 = enmascarada), pestillos y serie
  uint8_t  imask = 7;
  bool     r75 = false, trap = false;      // pestillos de RST 7.5 y TRAP
  bool     irq55 = false, irq65 = false;   // líneas (por nivel) de RST 5.5 y 6.5
  bool     sid = true;                     // entrada serie (en reposo = 1)
  bool     sod = false;                    // salida serie
  uint8_t  curOp = 0;                      // último opcode y si el salto se tomó (para los tiempos del 8085)
  bool     taken = false;
  void pulseRst75() { r75 = true; }
  void pulseTrap()  { trap = true; }

  // Conexiones con el bus de la placa
  uint8_t (*rd)(uint16_t addr);
  void    (*wr)(uint16_t addr, uint8_t v);
  uint8_t (*in)(uint8_t port);
  void    (*out)(uint8_t port, uint8_t v);

  void reset();
  int  step();         // ejecuta una instrucción (o atiende una interrupción), devuelve estados T
  int  exec();
};

enum : uint8_t { F_C = 0x01, F_P = 0x04, F_A = 0x10, F_Z = 0x40, F_S = 0x80 };

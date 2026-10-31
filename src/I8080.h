// Núcleo Intel 8080 / NEC uPD8080A, independiente de Arduino (se puede
// compilar y probar en el PC). Contabiliza estados T por instrucción.
#pragma once
#include <stdint.h>

struct I8080 {
  // Registros: r[0..7] = B,C,D,E,H,L,(M no usado),A
  uint8_t  r[8];
  uint16_t sp, pc;
  uint8_t  f;          // S Z 0 AC 0 P 1 C
  bool     inte;       // flip-flop de interrupciones
  bool     halted;

  // Conexiones con el bus de la placa
  uint8_t (*rd)(uint16_t addr);
  void    (*wr)(uint16_t addr, uint8_t v);
  uint8_t (*in)(uint8_t port);
  void    (*out)(uint8_t port, uint8_t v);

  void reset();
  int  step();         // ejecuta una instrucción, devuelve estados T
};

enum : uint8_t { F_C = 0x01, F_P = 0x04, F_A = 0x10, F_Z = 0x40, F_S = 0x80 };

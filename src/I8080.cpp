#include "I8080.h"

enum { rB = 0, rC, rD, rE, rH, rL, rM, rA };

static inline bool parityEven(uint8_t v) { return !__builtin_parity(v); }

static inline uint8_t szp(uint8_t v) {
  return (v & F_S) | (v == 0 ? F_Z : 0) | (parityEven(v) ? F_P : 0);
}

void I8080::reset() {
  for (int i = 0; i < 8; ++i) r[i] = 0;
  sp = 0; pc = 0; f = 0x02; inte = false; halted = false;
}

#define HL()    ((uint16_t)((r[rH] << 8) | r[rL]))
#define SETHL(v) do { uint16_t _v = (v); r[rH] = _v >> 8; r[rL] = _v & 0xFF; } while (0)

int I8080::step() {
  if (halted) return 4;   // sin interrupciones externas modeladas

  auto fetch8  = [&]() -> uint8_t { return rd(pc++); };
  auto fetch16 = [&]() -> uint16_t { uint8_t l = rd(pc++); uint8_t h = rd(pc++); return (uint16_t)((h << 8) | l); };
  auto push16  = [&](uint16_t v) { wr(--sp, v >> 8); wr(--sp, v & 0xFF); };
  auto pop16   = [&]() -> uint16_t { uint8_t l = rd(sp++); uint8_t h = rd(sp++); return (uint16_t)((h << 8) | l); };
  auto getRP   = [&](int p) -> uint16_t {
    switch (p) { case 0: return (r[rB] << 8) | r[rC]; case 1: return (r[rD] << 8) | r[rE];
                 case 2: return HL(); default: return sp; } };
  auto setRP   = [&](int p, uint16_t v) {
    switch (p) { case 0: r[rB] = v >> 8; r[rC] = v & 0xFF; break;
                 case 1: r[rD] = v >> 8; r[rE] = v & 0xFF; break;
                 case 2: SETHL(v); break; default: sp = v; } };
  auto cond    = [&](int cc) -> bool {
    switch (cc) { case 0: return !(f & F_Z); case 1: return f & F_Z;
                  case 2: return !(f & F_C); case 3: return f & F_C;
                  case 4: return !(f & F_P); case 5: return f & F_P;
                  case 6: return !(f & F_S); default: return f & F_S; } };

  // Operación aritmético-lógica sobre el acumulador
  auto alu = [&](int op, uint8_t v) {
    uint8_t a = r[rA];
    int cin = (f & F_C) ? 1 : 0;
    switch (op) {
      case 0: case 1: {                       // ADD, ADC
        int c = (op == 1) ? cin : 0;
        int res = a + v + c;
        uint8_t o = res & 0xFF;
        f = szp(o) | 0x02 | (res > 0xFF ? F_C : 0) | (((a & 0xF) + (v & 0xF) + c) > 0xF ? F_A : 0);
        r[rA] = o; break; }
      case 2: case 3: case 7: {               // SUB, SBB, CMP
        int c = (op == 3) ? cin : 0;
        int res = a - v - c;
        uint8_t o = res & 0xFF;
        f = szp(o) | 0x02 | (res < 0 ? F_C : 0) | (((a & 0xF) - (v & 0xF) - c) >= 0 ? F_A : 0);
        if (op != 7) r[rA] = o;
        break; }
      case 4: {                               // ANA (AC = OR de los bits 3, como el 8080)
        uint8_t o = a & v;
        f = szp(o) | 0x02 | (((a | v) & 0x08) ? F_A : 0);
        r[rA] = o; break; }
      case 5: { uint8_t o = a ^ v; f = szp(o) | 0x02; r[rA] = o; break; }   // XRA
      case 6: { uint8_t o = a | v; f = szp(o) | 0x02; r[rA] = o; break; }   // ORA
    }
  };

  uint8_t op = fetch8();

  // ---- MOV / HLT ----
  if ((op & 0xC0) == 0x40) {
    if (op == 0x76) { halted = true; return 7; }
    int d = (op >> 3) & 7, s = op & 7;
    uint8_t v = (s == rM) ? rd(HL()) : r[s];
    if (d == rM) wr(HL(), v); else r[d] = v;
    return (d == rM || s == rM) ? 7 : 5;
  }
  // ---- ALU registro/memoria ----
  if ((op & 0xC0) == 0x80) {
    int s = op & 7;
    alu((op >> 3) & 7, (s == rM) ? rd(HL()) : r[s]);
    return s == rM ? 7 : 4;
  }

  switch (op) {
    // ---- 0x00-0x3F ----
    case 0x00: case 0x08: case 0x10: case 0x18: case 0x20: case 0x28: case 0x30: case 0x38:
      return 4;                                                    // NOP (y alias no documentados)
    case 0x01: case 0x11: case 0x21: case 0x31: setRP((op >> 4) & 3, fetch16()); return 10; // LXI
    case 0x09: case 0x19: case 0x29: case 0x39: {                  // DAD
      uint32_t res = (uint32_t)HL() + getRP((op >> 4) & 3);
      SETHL(res & 0xFFFF);
      f = (f & ~F_C) | (res > 0xFFFF ? F_C : 0);
      return 10; }
    case 0x02: wr((r[rB] << 8) | r[rC], r[rA]); return 7;          // STAX B
    case 0x12: wr((r[rD] << 8) | r[rE], r[rA]); return 7;          // STAX D
    case 0x0A: r[rA] = rd((r[rB] << 8) | r[rC]); return 7;         // LDAX B
    case 0x1A: r[rA] = rd((r[rD] << 8) | r[rE]); return 7;         // LDAX D
    case 0x22: { uint16_t a = fetch16(); wr(a, r[rL]); wr(a + 1, r[rH]); return 16; }  // SHLD
    case 0x2A: { uint16_t a = fetch16(); r[rL] = rd(a); r[rH] = rd(a + 1); return 16; } // LHLD
    case 0x32: wr(fetch16(), r[rA]); return 13;                    // STA
    case 0x3A: r[rA] = rd(fetch16()); return 13;                   // LDA
    case 0x03: case 0x13: case 0x23: case 0x33: { int p = (op >> 4) & 3; setRP(p, getRP(p) + 1); return 5; } // INX
    case 0x0B: case 0x1B: case 0x2B: case 0x3B: { int p = (op >> 4) & 3; setRP(p, getRP(p) - 1); return 5; } // DCX
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C: { // INR
      int d = (op >> 3) & 7;
      uint8_t v = (d == rM) ? rd(HL()) : r[d];
      uint8_t o = v + 1;
      f = (f & F_C) | szp(o) | 0x02 | (((v & 0xF) == 0xF) ? F_A : 0);
      if (d == rM) wr(HL(), o); else r[d] = o;
      return d == rM ? 10 : 5; }
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D: { // DCR
      int d = (op >> 3) & 7;
      uint8_t v = (d == rM) ? rd(HL()) : r[d];
      uint8_t o = v - 1;
      f = (f & F_C) | szp(o) | 0x02 | (((v & 0xF) != 0) ? F_A : 0);
      if (d == rM) wr(HL(), o); else r[d] = o;
      return d == rM ? 10 : 5; }
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E: { // MVI
      int d = (op >> 3) & 7; uint8_t v = fetch8();
      if (d == rM) { wr(HL(), v); return 10; }
      r[d] = v; return 7; }
    case 0x07: { uint8_t c = r[rA] >> 7; r[rA] = (r[rA] << 1) | c; f = (f & ~F_C) | c; return 4; }          // RLC
    case 0x0F: { uint8_t c = r[rA] & 1;  r[rA] = (r[rA] >> 1) | (c << 7); f = (f & ~F_C) | c; return 4; }   // RRC
    case 0x17: { uint8_t c = r[rA] >> 7; r[rA] = (r[rA] << 1) | (f & F_C); f = (f & ~F_C) | c; return 4; }  // RAL
    case 0x1F: { uint8_t c = r[rA] & 1;  r[rA] = (r[rA] >> 1) | ((f & F_C) << 7); f = (f & ~F_C) | c; return 4; } // RAR
    case 0x27: {                                                   // DAA
      uint8_t a = r[rA], corr = 0; bool carry = f & F_C;
      if ((a & 0xF) > 9 || (f & F_A)) corr |= 0x06;
      if (a > 0x99 || carry) { corr |= 0x60; carry = true; }
      uint8_t o = a + corr;
      f = szp(o) | 0x02 | (carry ? F_C : 0) | ((((a & 0xF) + (corr & 0xF)) > 0xF) ? F_A : 0);
      r[rA] = o; return 4; }
    case 0x2F: r[rA] = ~r[rA]; return 4;                           // CMA
    case 0x37: f |= F_C; return 4;                                 // STC
    case 0x3F: f ^= F_C; return 4;                                 // CMC

    // ---- 0xC0-0xFF ----
    case 0xC0: case 0xC8: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8: // Rcc
      if (cond((op >> 3) & 7)) { pc = pop16(); return 11; }
      return 5;
    case 0xC1: case 0xD1: case 0xE1: { setRP((op >> 4) & 3, pop16()); return 10; }          // POP B/D/H
    case 0xF1: { uint16_t v = pop16(); f = ((v & 0xD5) | 0x02); r[rA] = v >> 8; return 10; } // POP PSW
    case 0xC5: case 0xD5: case 0xE5: push16(getRP((op >> 4) & 3)); return 11;               // PUSH B/D/H
    case 0xF5: push16((r[rA] << 8) | ((f & 0xD5) | 0x02)); return 11;                       // PUSH PSW
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: case 0xE2: case 0xEA: case 0xF2: case 0xFA: { // Jcc
      uint16_t a = fetch16(); if (cond((op >> 3) & 7)) pc = a; return 10; }
    case 0xC3: case 0xCB: pc = fetch16(); return 10;                                         // JMP
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: case 0xE4: case 0xEC: case 0xF4: case 0xFC: { // Ccc
      uint16_t a = fetch16();
      if (cond((op >> 3) & 7)) { push16(pc); pc = a; return 17; }
      return 11; }
    case 0xC6: alu(0, fetch8()); return 7;                         // ADI
    case 0xCE: alu(1, fetch8()); return 7;                         // ACI
    case 0xD6: alu(2, fetch8()); return 7;                         // SUI
    case 0xDE: alu(3, fetch8()); return 7;                         // SBI
    case 0xE6: alu(4, fetch8()); return 7;                         // ANI
    case 0xEE: alu(5, fetch8()); return 7;                         // XRI
    case 0xF6: alu(6, fetch8()); return 7;                         // ORI
    case 0xFE: alu(7, fetch8()); return 7;                         // CPI
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF: // RST
      push16(pc); pc = op & 0x38; return 11;
    case 0xC9: case 0xD9: pc = pop16(); return 10;                 // RET
    case 0xCD: case 0xDD: case 0xED: case 0xFD: { uint16_t a = fetch16(); push16(pc); pc = a; return 17; } // CALL
    case 0xD3: { uint8_t p = fetch8(); out(p, r[rA]); return 10; } // OUT
    case 0xDB: { uint8_t p = fetch8(); r[rA] = in(p); return 10; } // IN
    case 0xE3: {                                                   // XTHL
      uint8_t l = rd(sp), h = rd(sp + 1);
      wr(sp, r[rL]); wr(sp + 1, r[rH]); r[rL] = l; r[rH] = h; return 18; }
    case 0xE9: pc = HL(); return 5;                                // PCHL
    case 0xEB: { uint8_t t;                                        // XCHG
      t = r[rH]; r[rH] = r[rD]; r[rD] = t; t = r[rL]; r[rL] = r[rE]; r[rE] = t; return 4; }
    case 0xF3: inte = false; return 4;                             // DI
    case 0xFB: inte = true;  return 4;                             // EI
    case 0xF9: sp = HL(); return 5;                                // SPHL
  }
  return 4;
}

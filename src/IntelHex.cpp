#include "IntelHex.h"
#include "TK80Core.h"
#include <stdio.h>
#include <string.h>

static int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static bool byteAt(const char* line, size_t n, size_t pos, uint8_t& v) {
  if (pos + 1 >= n) return false;
  int hi = nibble(line[pos]), lo = nibble(line[pos + 1]);
  if (hi < 0 || lo < 0) return false;
  v = (uint8_t)((hi << 4) | lo);
  return true;
}

static bool fail(HexResult& r, const char* msg) {
  snprintf(r.error, sizeof(r.error), "%s", msg);
  return false;
}

bool intelHexProcess(const char* text, size_t len, bool apply, HexResult& r) {
  r = HexResult();
  uint32_t base = 0;
  bool sawEOF = false;
  size_t pos = 0;

  while (pos < len) {
    size_t end = pos;
    while (end < len && text[end] != '\n') ++end;
    const char* line = text + pos;
    size_t n = end - pos;
    pos = end < len ? end + 1 : len;
    while (n && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) --n;
    while (n && (*line == ' ' || *line == '\t')) { ++line; --n; }
    if (!n) continue;
    if (sawEOF) return fail(r, "Hay datos después del registro EOF");
    if (line[0] != ':') return fail(r, "Registro Intel HEX sin ':' inicial");

    uint8_t count, hi, lo, type;
    if (!byteAt(line, n, 1, count) || !byteAt(line, n, 3, hi) ||
        !byteAt(line, n, 5, lo) || !byteAt(line, n, 7, type))
      return fail(r, "Cabecera Intel HEX no válida");
    if (n != 11u + (size_t)count * 2u) return fail(r, "Longitud de registro incorrecta");

    uint8_t sum = (uint8_t)(count + hi + lo + type), b = 0;
    for (uint8_t i = 0; i < count; ++i) {
      if (!byteAt(line, n, 9u + (size_t)i * 2u, b)) return fail(r, "Dato hexadecimal no válido");
      sum = (uint8_t)(sum + b);
    }
    uint8_t chk;
    if (!byteAt(line, n, 9u + (size_t)count * 2u, chk) || (uint8_t)(sum + chk) != 0)
      return fail(r, "Checksum incorrecto en un registro");

    const uint16_t offset = (uint16_t)((hi << 8) | lo);
    if (type == 0x00) {
      for (uint8_t i = 0; i < count; ++i) {
        uint32_t a = base + offset + i;
        if (a > 0xFFFFu) return fail(r, "Dirección fuera del espacio de 16 bits");
        if (!tk80::isLoadable((uint16_t)a)) {
          snprintf(r.error, sizeof(r.error), "Dirección %04X no escribible (ROM o sin memoria)", (unsigned)a);
          return false;
        }
        if (r.firstAddr == 0xFFFFFFFFu) r.firstAddr = a;
        if (apply) { byteAt(line, n, 9u + (size_t)i * 2u, b); tk80::loadByte((uint16_t)a, b); }
      }
      r.bytes += count;
    } else if (type == 0x01) {
      if (count != 0 || offset != 0) return fail(r, "Registro EOF no válido");
      sawEOF = true;
    } else if (type == 0x02) {
      uint8_t sh, sl;
      if (count != 2 || !byteAt(line, n, 9, sh) || !byteAt(line, n, 11, sl))
        return fail(r, "Registro de dirección segmentada no válido");
      base = (uint32_t)(((uint16_t)sh << 8) | sl) << 4;
    } else if (type == 0x04) {
      uint8_t uh, ul;
      if (count != 2 || !byteAt(line, n, 9, uh) || !byteAt(line, n, 11, ul))
        return fail(r, "Registro de dirección lineal no válido");
      if (uh || ul) return fail(r, "La dirección extendida supera los 16 bits del TK-80");
      base = 0;
    } else if (type == 0x03 || type == 0x05) {
      // Dirección de arranque: válida, pero no se ejecuta; el arranque lo da la tecla RUN.
    } else {
      return fail(r, "Tipo de registro Intel HEX no compatible");
    }
    ++r.records;
  }
  if (!sawEOF) return fail(r, "Falta el registro EOF (:00000001FF)");
  if (r.bytes == 0) return fail(r, "El archivo no contiene datos");
  return true;
}

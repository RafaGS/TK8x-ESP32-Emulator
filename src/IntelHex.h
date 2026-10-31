// Validación y carga de archivos Intel HEX (portable, sin heap).
#pragma once
#include <stddef.h>
#include <stdint.h>

struct HexResult {
  size_t   bytes = 0;            // bytes de datos
  size_t   records = 0;          // registros procesados
  uint32_t firstAddr = 0xFFFFFFFFu;  // dirección del primer byte de datos
  char     error[72] = {0};      // vacío si todo fue bien
};

// Pasada de validación (apply=false) o de carga (apply=true). Hay que validar
// el archivo completo antes de cargarlo para no dejar la memoria a medias.
// Solo se admiten direcciones escribibles de la placa (RAM y EEPROM).
bool intelHexProcess(const char* text, size_t len, bool apply, HexResult& result);

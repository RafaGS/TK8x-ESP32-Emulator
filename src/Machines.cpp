// Descripciones de las máquinas. Datos de TK-80 de notwendig/tk80; datos de TK-85 de la ROM
// (tk85.bin) y del "NEC TK-85 Training Book" (mapa de memoria, p. 106-107).
#include "Machine.h"
#include "tk80rom.h"
#include "tk85rom.h"
#include <string.h>
#include <strings.h>

namespace machine {

// Dígitos 0-F: líneas 0 y 1 de la matriz, en el mismo orden en ambas máquinas.
#define HEX_KEYS \
  {"0",0,0},{"1",0,1},{"2",0,2},{"3",0,3},{"4",0,4},{"5",0,5},{"6",0,6},{"7",0,7}, \
  {"8",1,0},{"9",1,1},{"a",1,2},{"b",1,3},{"c",1,4},{"d",1,5},{"e",1,6},{"f",1,7}

// ------------------------------------------------------------------ TK-80
static const MatrixKey TK80_MATRIX[] = {
  HEX_KEYS,
  {"run",2,0},{"ret",2,1},{"adrs",2,2},{"rdec",2,3},{"rinc",2,4},{"winc",2,5},
};
static const UiKey TK80_UI[] = {
  {'f',"ret","RET",""}, {'f',"run","RUN",""}, {'t',"store","DATA","STORE",true}, {'t',"load","DATA","LOAD",true}, {'r',"reset","RESET",""},
  {'h',"c","C",""}, {'h',"d","D",""}, {'h',"e","E",""}, {'h',"f","F",""}, {'f',"adrs","SET","ADRS",true},
  {'h',"8","8",""}, {'h',"9","9",""}, {'h',"a","A",""}, {'h',"b","B",""}, {'f',"rinc","INC","READ",true},
  {'h',"4","4",""}, {'h',"5","5",""}, {'h',"6","6",""}, {'h',"7","7",""}, {'f',"rdec","DEC","READ",true},
  {'h',"0","0",""}, {'h',"1","1",""}, {'h',"2","2",""}, {'h',"3","3",""}, {'f',"winc","INC","WRITE",true},
};

// ------------------------------------------------------------------ TK-85
static const MatrixKey TK85_MATRIX[] = {
  HEX_KEYS,
  {"run",2,0},{"cont",2,1},{"adrs",2,2},{"rdec",2,3},{"rinc",2,4},{"wr",2,5},{"mode",2,6},{"reg",2,7},
  {"mon",0xFF,0,true},               // MON no está en la matriz: el circuito de TRAP del manual interrumpe a la CPU
};
// Teclado según la foto de la placa. RESET es un pulsador aparte: va en una sexta fila, bajo WR/ENT.
static const UiKey TK85_UI[] = {
  {'f',"cont","CONT",""}, {'f',"run","RUN",""}, {'f',"mode","MODE",""}, {'f',"reg","REG",""}, {'f',"mon","MON",""},
  {'h',"c","C","TM"}, {'h',"d","D","MOV"}, {'h',"e","E","OUT"}, {'h',"f","F","IN"}, {'f',"adrs","SET","ADRS",true},
  {'h',"8","8",""}, {'h',"9","9",""}, {'h',"a","A","SAVE"}, {'h',"b","B","LOAD"}, {'f',"rinc","INC","READ",true},
  {'h',"4","4","SP"}, {'h',"5","5","BR.P"}, {'h',"6","6","BR.D"}, {'h',"7","7",""}, {'f',"rdec","DEC","READ",true},
  {'h',"0","0","AF"}, {'h',"1","1","BC"}, {'h',"2","2","DE"}, {'h',"3","3","HL"}, {'f',"wr","ENT","WR",true},
  {' ',"","",""}, {' ',"","",""}, {' ',"","",""}, {' ',"","",""}, {'r',"reset","RESET",""},
};

#define COUNT(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

static const Spec SPECS[] = {
  { "tk80", "NEC TK-80", CpuVariant::I8080, 2048,
    tk80rom, sizeof(tk80rom),
    0x0300, 0x0100,                    // EEPROM de usuario
    0x8000, 0x0400,                    // RAM de 1 KiB
    0x83F8, 0x83EC, 0x83EE,
    TK80_MATRIX, COUNT(TK80_MATRIX), true,
    TK80_UI, COUNT(TK80_UI), 5 },
  { "tk85", "NEC TK-85", CpuVariant::I8085, 2458,      // 2,4576 MHz
    tk85rom, sizeof(tk85rom),
    0x0800, 0x1800,                    // PROM de usuario opcional (0800h-1FFFh)
    0x8000, 0x0400,                    // RAM estándar de 1 KiB (ampliable hasta 8FFFh)
    0x83F8, 0x83EC, 0x83EE,
    TK85_MATRIX, COUNT(TK85_MATRIX), false,
    TK85_UI, COUNT(TK85_UI), 5 },
};

size_t count() { return sizeof(SPECS) / sizeof(SPECS[0]); }
const Spec* at(size_t i) { return i < count() ? &SPECS[i] : nullptr; }
const Spec* find(const char* id) {
  if (!id) return nullptr;
  for (const Spec& s : SPECS) if (!strcasecmp(id, s.id)) return &s;
  return nullptr;
}

static void appendJsonString(std::string& out, const char* s) {
  out += '"';
  for (; *s; ++s) {
    if (*s == '"' || *s == '\\') out += '\\';
    out += *s;
  }
  out += '"';
}

std::string describeJson(const Spec& s) {
  std::string j = "{\"id\":";
  appendJsonString(j, s.id);
  j += ",\"name\":"; appendJsonString(j, s.name);
  j += ",\"clockKHz\":" + std::to_string(s.clockKHz);
  j += std::string(",\"tape\":") + (s.tapeKeys ? "true" : "false");
  j += ",\"cols\":" + std::to_string((unsigned)s.uiCols);
  j += ",\"keys\":[";
  for (uint8_t i = 0; i < s.nUi; ++i) {
    const UiKey& k = s.ui[i];
    if (i) j += ',';
    j += "{\"t\":"; appendJsonString(j, std::string(1, k.kind).c_str());
    j += ",\"k\":"; appendJsonString(j, k.name);
    j += ",\"l\":"; appendJsonString(j, k.label);
    j += ",\"s\":"; appendJsonString(j, k.sub);
    if (k.sameSize) j += ",\"e\":true";          // dos líneas del mismo tamaño (sin <small>)
    j += '}';
  }
  j += "],\"machines\":[";
  for (size_t i = 0; i < count(); ++i) {
    if (i) j += ',';
    j += "{\"id\":"; appendJsonString(j, SPECS[i].id);
    j += ",\"name\":"; appendJsonString(j, SPECS[i].name);
    j += '}';
  }
  j += "]}";
  return j;
}

}  // namespace machine

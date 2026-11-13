// Pruebas en el PC: núcleo 8080/8085 y placas TK-80 y TK-85 con sus ROM reales.
//   g++ -std=c++17 -Isrc -Iinclude tools/host_test.cpp src/I8080.cpp src/Machine.cpp src/Machines.cpp src/IntelHex.cpp src/App.cpp -o host_test
#include "I8080.h"
#include "Machine.h"
#include "IntelHex.h"
#include "App.h"
#include "config.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FALLO línea %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ---------- memoria plana para probar la CPU sola ----------
static uint8_t mem[65536];
static uint8_t flatRd(uint16_t a) { return mem[a]; }
static void    flatWr(uint16_t a, uint8_t v) { mem[a] = v; }
static uint8_t flatIn(uint8_t) { return 0x5A; }
static uint8_t lastOutPort, lastOutVal;
static void    flatOut(uint8_t p, uint8_t v) { lastOutPort = p; lastOutVal = v; }

static I8080 c;
static int runProg(std::initializer_list<uint8_t> code, int maxInstr = 1000) {
  memset(mem, 0, sizeof(mem));
  int i = 0; for (uint8_t b : code) mem[0x100 + i++] = b;
  c.rd = flatRd; c.wr = flatWr; c.in = flatIn; c.out = flatOut;
  c.reset(); c.pc = 0x100; c.sp = 0xF000;
  int t = 0;
  while (!c.halted && maxInstr--) t += c.step();
  return t;
}
enum { B, C, D, E, H, L, M, A };

static void cpuTests() {
  // BCD: 99 + 01 -> DAA
  runProg({0x3E,0x99, 0xC6,0x01, 0x27, 0x76});
  CHECK(c.r[A]==0x00 && (c.f&F_C) && (c.f&F_Z), "DAA 99+01 A=%02X F=%02X", c.r[A], c.f);
  // 19 + 28 -> DAA = 47
  runProg({0x3E,0x19, 0xC6,0x28, 0x27, 0x76});
  CHECK(c.r[A]==0x47 && !(c.f&F_C), "DAA 19+28 A=%02X F=%02X", c.r[A], c.f);
  // FF + 01
  runProg({0x3E,0xFF, 0xC6,0x01, 0x76});
  CHECK(c.r[A]==0 && (c.f&F_Z) && (c.f&F_P) && (c.f&F_C) && (c.f&F_A) && !(c.f&F_S), "ADI FF+1 F=%02X", c.f);
  // CPI
  runProg({0x3E,0x05, 0xFE,0x06, 0x76});
  CHECK(c.r[A]==5 && (c.f&F_C) && (c.f&F_S) && !(c.f&F_Z), "CPI 5,6 F=%02X", c.f);
  runProg({0x3E,0x05, 0xFE,0x05, 0x76});
  CHECK((c.f&F_Z) && !(c.f&F_C), "CPI 5,5 F=%02X", c.f);
  // SBI con acarreo: 00 - 00 - 1
  runProg({0x37, 0x3E,0x00, 0xDE,0x00, 0x76});
  CHECK(c.r[A]==0xFF && (c.f&F_C) && (c.f&F_S), "SBI A=%02X F=%02X", c.r[A], c.f);
  // SUB AC: 0x10 - 0x01 -> AC=0 (hay préstamo del bit 4)
  runProg({0x3E,0x10, 0xD6,0x01, 0x76});
  CHECK(c.r[A]==0x0F && !(c.f&F_A) && !(c.f&F_C), "SUI 10-01 F=%02X", c.f);
  runProg({0x3E,0x11, 0xD6,0x01, 0x76});
  CHECK(c.r[A]==0x10 && (c.f&F_A), "SUI 11-01 F=%02X", c.f);
  // ANA / XRA / ORA
  runProg({0x3E,0xF0, 0xE6,0x0F, 0x76});
  CHECK(c.r[A]==0 && (c.f&F_Z) && !(c.f&F_C), "ANI F=%02X", c.f);
  runProg({0x37, 0x3E,0xAA, 0xEE,0xAA, 0x76});
  CHECK(c.r[A]==0 && !(c.f&F_C) && (c.f&F_P), "XRI F=%02X", c.f);
  // Rotaciones
  runProg({0x3E,0x81, 0x07, 0x76});   CHECK(c.r[A]==0x03 && (c.f&F_C), "RLC");
  runProg({0x3E,0x81, 0x0F, 0x76});   CHECK(c.r[A]==0xC0 && (c.f&F_C), "RRC");
  runProg({0x37, 0x3E,0x01, 0x17, 0x76}); CHECK(c.r[A]==0x03 && !(c.f&F_C), "RAL");
  runProg({0x37, 0x3E,0x02, 0x1F, 0x76}); CHECK(c.r[A]==0x81 && !(c.f&F_C), "RAR");
  // DAD, XCHG, SPHL, PCHL
  runProg({0x21,0xFF,0xFF, 0x01,0x01,0x00, 0x09, 0x76});
  CHECK(c.r[H]==0 && c.r[L]==0 && (c.f&F_C), "DAD");
  runProg({0x21,0x34,0x12, 0x11,0x78,0x56, 0xEB, 0x76});
  CHECK(c.r[H]==0x56 && c.r[L]==0x78 && c.r[D]==0x12 && c.r[E]==0x34, "XCHG");
  // PUSH PSW / POP PSW
  runProg({0x3E,0x99, 0xC6,0x01, 0xF5, 0xAF, 0xF1, 0x76});
  CHECK(c.r[A]==0x9A && (c.f&F_S) && (c.f&F_P)==0 + (c.f&F_P), "PUSH/POP PSW A=%02X F=%02X", c.r[A], c.f);
  CHECK((c.f & 0x2A) == 0x02, "bits fijos de F: %02X", c.f);
  // XTHL
  runProg({0x31,0x00,0x20, 0x21,0x11,0x22, 0xE5, 0x21,0x33,0x44, 0xE3, 0x76});
  CHECK(c.r[H]==0x22 && c.r[L]==0x11 && mem[0x1FFE]==0x33 && mem[0x1FFF]==0x44, "XTHL");
  // CALL / RET y ciclos: LXI SP(10) CALL(17) RET(10) HLT(7) ; subrutina en 0x120
  memset(mem,0,sizeof(mem));
  { uint8_t p[] = {0x31,0x00,0x20, 0xCD,0x20,0x01, 0x76}; memcpy(mem+0x100,p,sizeof p); mem[0x120]=0xC9; }
  c.rd=flatRd; c.wr=flatWr; c.in=flatIn; c.out=flatOut; c.reset(); c.pc=0x100;
  int t=0; while(!c.halted) t+=c.step();
  CHECK(t==10+17+10+7, "ciclos CALL/RET: %d", t);
  // INR M / DCR M / SHLD / LHLD
  runProg({0x21,0x00,0x30, 0x36,0x0F, 0x34, 0x76});
  CHECK(mem[0x3000]==0x10 && (c.f&F_A), "INR M");
  runProg({0x21,0x34,0x12, 0x22,0x00,0x30, 0x21,0x00,0x00, 0x2A,0x00,0x30, 0x76});
  CHECK(c.r[H]==0x12 && c.r[L]==0x34, "SHLD/LHLD");
  // Saltos condicionales: contar 5 iteraciones
  runProg({0x06,0x05, 0x3E,0x00, 0xC6,0x02, 0x05, 0xC2,0x04,0x01, 0x76});
  CHECK(c.r[A]==10, "bucle DCR/JNZ A=%d", c.r[A]);
  // IN / OUT
  runProg({0xDB,0x10, 0xD3,0x77, 0x76});
  CHECK(c.r[A]==0x5A && lastOutPort==0x77 && lastOutVal==0x5A, "IN/OUT");
  // RST
  memset(mem,0,sizeof(mem)); mem[0x100]=0xFF; mem[0x38]=0x76;
  c.rd=flatRd; c.wr=flatWr; c.reset(); c.pc=0x100; c.sp=0x2000;
  while(!c.halted) c.step();
  CHECK(c.pc==0x39 && mem[0x1FFE]==0x01 && mem[0x1FFF]==0x01, "RST 7");
}

// ---------- placa TK-80 con la ROM real ----------
static void runCycles(long n) { while (n > 0) { int t = machine::step(); n -= t ? t : 1; } }

static std::string disp() {
  static const struct { uint8_t seg; char ch; } T[] = {
    {0x5C,'0'},{0x3F,'0'},{0x06,'1'},{0x5B,'2'},{0x4F,'3'},{0x66,'4'},{0x6D,'5'},{0x7D,'6'},{0x27,'7'},
    {0x7F,'8'},{0x6F,'9'},{0x77,'A'},{0x7C,'b'},{0x39,'C'},{0x5E,'d'},{0x79,'E'},{0x71,'F'},{0x00,' '},
  };
  std::string s;
  const uint8_t* d = machine::display();
  for (int i = 0; i < 8; ++i) {
    char ch = '?';
    for (auto& e : T) if (e.seg == (d[i] & 0x7F)) { ch = e.ch; break; }
    s += ch; if (d[i] & 0x80) s += '.';
    if (i == 3) s += ' ';
  }
  return s;
}

static void press(const char* k) {
  machine::setKey(k, true);  runCycles(60000);   // ~60 ms de tecla pulsada
  machine::setKey(k, false); runCycles(60000);
}
static void pressSeq(const char* keys) { for (const char* p = keys; *p; ++p) { char k[2] = {*p, 0}; press(k); } }

static void boardTests() {
  machine::init("tk80");
  runCycles(200000);
  printf("tras reset: [%s]\n", disp().c_str());

  // En el TK-80 se teclea el valor y luego se pulsa ADRS SET (los dígitos 4-7 son el dato tecleado)
  pressSeq("8000");
  std::string d = disp(); printf("tecleado 8000: [%s]\n", d.c_str());
  CHECK(d.substr(5,4)=="8000", "valor tecleado: %s", d.c_str());
  press("adrs");
  d = disp(); printf("ADRS SET: [%s]\n", d.c_str());
  CHECK(d.substr(0,4)=="8000", "dirección mostrada: %s", d.c_str());

  // Escribir datos con WRITE INC: 3E 55 -> en 8000h y 8001h
  pressSeq("3e"); press("winc"); pressSeq("55"); press("winc");
  CHECK(machine::read(0x8000)==0x3E && machine::read(0x8001)==0x55, "WRITE INC: %02X %02X", machine::read(0x8000), machine::read(0x8001));
  d = disp(); printf("tras 2 escrituras: [%s]\n", d.c_str());

  // READ INC / READ DEC
  pressSeq("8000"); press("adrs"); press("rinc");
  d = disp(); printf("READ INC desde 8000: [%s]\n", d.c_str());
  CHECK(d.substr(0,4)=="8001" && d.substr(7,2)=="55", "READ INC: %s", d.c_str());

  // Programa cargado "por HEX": muestra 0 y 1 en los dos primeros dígitos y se queda en bucle
  const uint8_t prog[] = {0x21,0xF8,0x83, 0x36,0x3F, 0x23, 0x36,0x06, 0xC3,0x08,0x80};
  for (size_t i = 0; i < sizeof prog; ++i) machine::loadByte(0x8100 + i, prog[i]);
  // el JMP apunta a sí mismo: 0x8108 contiene C3 08 81
  machine::loadByte(0x8109, 0x08); machine::loadByte(0x810A, 0x81);
  pressSeq("8100"); press("adrs"); press("run");
  runCycles(100000);
  d = disp(); printf("RUN 8100: [%s]  PC=%04X\n", d.c_str(), machine::cpuPC());
  CHECK(machine::display()[0]==0x3F && machine::display()[1]==0x06, "programa de usuario ejecutado");
  CHECK(machine::cpuPC()==0x8108, "PC en bucle: %04X", machine::cpuPC());

  // RESET: pulsar detiene, soltar arranca desde 0
  machine::setKey("reset", true);
  CHECK(machine::step()==0, "CPU parada con RESET pulsado");
  machine::setKey("reset", false); machine::pollReset(); runCycles(200000);
  d = disp(); printf("tras RESET: [%s]\n", d.c_str());

  // La ROM no se puede escribir, la EEPROM solo con el cargador
  machine::write(0x0000, 0x00); CHECK(machine::read(0)==0x3E, "ROM protegida");
  machine::write(0x0300, 0x12); CHECK(machine::read(0x300)==0xFF, "EEPROM protegida para la CPU");
  machine::loadByte(0x0300, 0x12); CHECK(machine::read(0x300)==0x12, "EEPROM cargable");
  CHECK(!machine::isLoadable(0x0100) && !machine::isLoadable(0x9000) && machine::isLoadable(0x83FF), "rangos cargables");
}

static void hexTests() {
  machine::init("tk80");
  std::ifstream fs("examples/hello.hex"); std::stringstream ss; ss << fs.rdbuf(); std::string h = ss.str();
  HexResult r;
  CHECK(intelHexProcess(h.c_str(), h.size(), false, r), "hello.hex rechazado: %s", r.error);
  CHECK(r.bytes==29 && r.firstAddr==0x8100, "bytes=%zu first=%04X", r.bytes, (unsigned)r.firstAddr);
  CHECK(machine::read(0x8100)==0xFF, "la validación no debe escribir");
  CHECK(intelHexProcess(h.c_str(), h.size(), true, r), "carga");
  CHECK(machine::read(0x8100)==0x21 && machine::read(0x811C)==0x81, "datos cargados");

  // Checksum mal: la memoria no debe cambiar
  std::string bad = h; bad[bad.find(":1D")+ 5] = '2';   // altera la dirección
  machine::init("tk80");
  CHECK(!intelHexProcess(bad.c_str(), bad.size(), false, r) && r.error[0], "checksum erróneo aceptado");
  printf("error esperado: %s\n", r.error);
  // Direcciones no escribibles
  std::string rom = ":0100000000FF\n:00000001FF\n";
  CHECK(!intelHexProcess(rom.c_str(), rom.size(), false, r), "ROM aceptada"); printf("error esperado: %s\n", r.error);
  std::string noeof = ":0180000055" "2A\n";
  CHECK(!intelHexProcess(noeof.c_str(), noeof.size(), false, r), "sin EOF aceptado"); printf("error esperado: %s\n", r.error);
  // Fin de línea CRLF y mayúsculas/minúsculas
  std::string crlf = ":0180000055" "2A\r\n:00000001FF\r\n";
  CHECK(intelHexProcess(crlf.c_str(), crlf.size(), false, r), "CRLF: %s", r.error);

  // Cargar hello.hex y ejecutarlo con el monitor
  machine::init("tk80"); runCycles(200000);
  intelHexProcess(h.c_str(), h.size(), true, r);
  pressSeq("8100"); press("adrs"); press("run"); runCycles(200000);
  static const uint8_t hello[8] = {0x76,0x79,0x38,0x38,0x3F,0,0,0};
  CHECK(memcmp(machine::display(), hello, 8)==0, "HELLO en el display");
  printf("hello.hex en ejecución: PC=%04X display=%02X %02X %02X %02X %02X\n", machine::cpuPC(),
         machine::display()[0],machine::display()[1],machine::display()[2],machine::display()[3],machine::display()[4]);
}

static void appTests() {
  app::init();
  std::ifstream fs("examples/hello.hex"); std::stringstream ss; ss << fs.rdbuf(); std::string h = ss.str();
  std::string body;
  int code = app::submitHex(std::string(h), body);
  CHECK(code==200 && body.find("\"start\":\"8100\"")!=std::string::npos, "submitHex: %d %s", code, body.c_str());
  CHECK(machine::read(0x8100)==0xFF, "submitHex no debe escribir hasta pump()");
  code = app::submitHex(std::string(h), body);
  CHECK(code==409, "segunda carga pendiente: %d", code);
  std::string bad = ":0100000000FF\n:00000001FF\n";
  code = app::submitHex(std::move(bad), body);   // 409 por la carga pendiente o 400 por ROM; nunca 200
  CHECK(code!=200, "HEX inválido aceptado");
  std::string big(MAX_HEX_UPLOAD + 1, 'x');
  CHECK(app::submitHex(std::move(big), body)==413, "archivo grande");

  // Reloj simulado: 1 ms por iteración. Debe aplicar el HEX y ejecutar la ROM a ~1,1 MHz.
  uint32_t t = 1000;
  for (int i = 0; i < 300; ++i, t += 1000) app::pump(t);
  CHECK(machine::read(0x8100)==0x21, "pump aplica el HEX");
  char json[80]; int n = app::displayJson(json, sizeof json);
  CHECK(n>0 && strncmp(json,"{\"d\":[",6)==0, "displayJson: %s", json);
  printf("displayJson: %s\n", json);
}

// Extrae el campo "hex" del JSON de tapeStore y deshace el escape \n
static std::string jsonHex(const std::string& body) {
  size_t a = body.find("\"hex\":\"");
  if (a == std::string::npos) return "";
  a += 7;
  std::string out;
  for (size_t i = a; i < body.size() && body[i] != '"'; ++i) {
    if (body[i] == '\\' && i + 1 < body.size() && body[i + 1] == 'n') { out += '\n'; ++i; }
    else out += body[i];
  }
  return out;
}

static void tapeTests() {
  machine::init("tk80"); runCycles(200000);
  std::ifstream fs("examples/hello.hex"); std::stringstream ss; ss << fs.rdbuf(); std::string h = ss.str();
  HexResult r; intelHexProcess(h.c_str(), h.size(), true, r);

  // Las teclas STORE/LOAD no llegan a la ROM: el monitor sigue respondiendo
  CHECK(machine::setKey("store", true) && machine::setKey("load", true), "store/load aceptadas");
  runCycles(300000);
  machine::setKey("store", false); machine::setKey("load", false); runCycles(60000);
  pressSeq("8100"); press("adrs");
  CHECK(disp().substr(0,4)=="8100", "el monitor sigue vivo tras STORE/LOAD: %s", disp().c_str());

  // Registros del monitor: ADRES = 8100 (ADRS SET), DATA = valor tecleado
  pressSeq("811C");
  CHECK(machine::readWord(machine::spec().monAdres)==0x8100 && machine::readWord(machine::spec().monData)==0x811C,
        "ADRES=%04X DATA=%04X", machine::readWord(machine::spec().monAdres), machine::readWord(machine::spec().monData));

  std::string body;
  int code = app::tapeStore(body);
  CHECK(code==200 && body.find("\"bytes\":29")!=std::string::npos && body.find("\"start\":\"8100\"")!=std::string::npos, "tapeStore: %d %s", code, body.c_str());
  std::string hex = jsonHex(body);
  HexResult v;
  CHECK(intelHexProcess(hex.c_str(), hex.size(), false, v) && v.bytes==29 && v.firstAddr==0x8100, "HEX generado válido: %s", v.error);

  // Ida y vuelta: se borra la memoria y LOAD (HEX guardado -> submitHex) la restaura
  for (int a = 0x8100; a <= 0x811C; ++a) machine::loadByte((uint16_t)a, 0xFF);
  CHECK(machine::read(0x8100)==0xFF, "memoria borrada");
  CHECK(app::submitHex(std::string(hex), body)==200, "LOAD: %s", body.c_str());
  uint32_t t = 1000000; for (int i = 0; i < 5; ++i, t += 1000) app::pump(t);
  CHECK(machine::read(0x8100)==0x21 && machine::read(0x811C)==0x81, "ida y vuelta");

  // Errores: rango invertido y rango excesivo
  machine::init("tk80"); runCycles(200000);
  pressSeq("8100"); press("adrs"); pressSeq("80FF");
  CHECK(app::tapeStore(body)==400 && body.find("menor")!=std::string::npos, "rango invertido: %s", body.c_str());
  pressSeq("0000"); press("adrs"); pressSeq("FFFF");
  CHECK(app::tapeStore(body)==400 && body.find("demasiado grande")!=std::string::npos, "rango grande: %s", body.c_str());
  printf("tapeStore ok: %s\n", body.c_str());
}

// ---------- 8085 ----------
static int runProg85(std::initializer_list<uint8_t> code, int maxInstr = 1000) {
  c.variant = CpuVariant::I8085;
  int t = runProg(code, maxInstr);
  c.variant = CpuVariant::I8080;
  return t;
}
static void cpu85Tests() {
  // Tiempos del 8085: MOV r,r 4 (8080: 5), INX 6, PUSH 12, HLT 5
  CHECK(runProg({0x78, 0x76}) == 5 + 7, "8080 MOV r,r + HLT");
  CHECK(runProg85({0x78, 0x76}) == 4 + 5, "8085 MOV r,r + HLT: %d", runProg85({0x78, 0x76}));
  CHECK(runProg85({0x23, 0x76}) == 6 + 5, "8085 INX");
  CHECK(runProg85({0xC5, 0x76}) == 12 + 5, "8085 PUSH");
  CHECK(runProg85({0x3C, 0x76}) == 4 + 5, "8085 INR r");
  // CALL 18, RET 10; Jcc no tomado 7, tomado 10
  memset(mem, 0, sizeof(mem));
  { uint8_t p[] = {0xCD, 0x20, 0x01, 0x76}; memcpy(mem + 0x100, p, sizeof p); mem[0x120] = 0xC9; }
  c.rd = flatRd; c.wr = flatWr; c.in = flatIn; c.out = flatOut; c.variant = CpuVariant::I8085; c.reset(); c.pc = 0x100; c.sp = 0xF000;
  int t = 0; while (!c.halted) t += c.step();
  CHECK(t == 18 + 10 + 5, "8085 CALL/RET: %d", t);
  c.variant = CpuVariant::I8080;
  CHECK(runProg85({0xAF, 0xC2, 0x00, 0x20, 0x76}) == 4 + 7 + 5, "8085 JNZ no tomado");   // XRA A pone Z
  CHECK(runProg85({0xAF, 0xCA, 0x04, 0x01, 0x76}) == 4 + 10 + 5, "8085 JZ tomado");

  // ANA/ANI: el 8085 pone AC = 1, el 8080 usa el bit 3
  runProg({0x3E, 0x00, 0xE6, 0x00, 0x76});   CHECK(!(c.f & F_A), "8080 ANI AC=0");
  runProg85({0x3E, 0x00, 0xE6, 0x00, 0x76}); CHECK((c.f & F_A), "8085 ANI AC=1");

  // RIM/SIM: en el 8080 son NOP
  runProg({0x3E, 0x55, 0x20, 0x76});          CHECK(c.r[A] == 0x55, "RIM en 8080 = NOP");
  // SIM carga máscaras (MSE=1, M7.5=1 M6.5=0 M5.5=1) y fija SOD
  runProg85({0xFB, 0x3E, 0xCD, 0x30, 0x20, 0x76});   // EI; MVI A,CDh; SIM; RIM
  CHECK((c.r[A] & 0x07) == 0x05 && (c.r[A] & 0x08) && (c.r[A] & 0x80) && c.sod, "RIM/SIM A=%02X sod=%d", c.r[A], c.sod);

  // Interrupciones: RST 7.5 (vector 3Ch) con IE y máscara abierta; TRAP (24h) sin condiciones
  memset(mem, 0, sizeof(mem));
  { uint8_t p[] = {0xFB, 0x3E, 0x08, 0x30, 0x00, 0x00, 0x00, 0x00}; memcpy(mem + 0x100, p, sizeof p); }
  mem[0x3C] = 0x76;
  c.rd = flatRd; c.wr = flatWr; c.in = flatIn; c.out = flatOut; c.variant = CpuVariant::I8085; c.reset(); c.pc = 0x100; c.sp = 0xF000;
  for (int i = 0; i < 4; ++i) c.step();      // EI, MVI A,08h, SIM (MSE, máscaras a 0), NOP
  c.pulseRst75();
  int ti = c.step();
  CHECK(ti == 12 && c.pc == 0x3C && !c.inte && mem[0xEFFE] == 0x05 && mem[0xEFFF] == 0x01, "RST 7.5: t=%d pc=%04X", ti, c.pc);
  c.reset(); c.pc = 0x200; c.sp = 0xF000; c.pulseTrap();
  CHECK(c.step() == 12 && c.pc == 0x24, "TRAP sin IE");
  c.variant = CpuVariant::I8080;
}

// ---------- TK-85 con la ROM real ----------
static void press85(const char* k) { machine::setKey(k, true); runCycles(400000); machine::setKey(k, false); runCycles(400000); }
static void pressSeq85(const char* keys) { for (const char* p = keys; *p; ++p) { char k[2] = {*p, 0}; press85(k); } }

static void tk85Tests() {
  CHECK(machine::init("tk85") && std::string(machine::spec().id) == "tk85", "init tk85");
  CHECK(machine::spec().cpu == CpuVariant::I8085 && machine::spec().clockKHz == 2458, "spec tk85");
  runCycles(600000);
  const uint8_t* d = machine::display();
  bool zeros = true; for (int i = 0; i < 8; ++i) zeros = zeros && d[i] == 0x3F;
  CHECK(zeros, "arranque: ocho ceros (3Fh): [%s]", disp().c_str());
  printf("TK-85 arranque: [%s]\n", disp().c_str());

  pressSeq85("12");
  CHECK(disp().substr(7,2)=="12", "los dígitos entran por la derecha: [%s]", disp().c_str());

  // Mapa de memoria del TK-85: PROM de usuario 0800h-1FFFh, RAM 8000h-83FFh
  CHECK(machine::isLoadable(0x0800) && machine::isLoadable(0x1FFF) && machine::isLoadable(0x8000) && machine::isLoadable(0x83FF) &&
        !machine::isLoadable(0x0300) && !machine::isLoadable(0x2000) && !machine::isLoadable(0x8400), "rangos cargables tk85");

  // ADRS SET, WR/ENT, READ INC/DEC y RUN con el monitor real
  pressSeq85("8000"); press85("adrs");
  CHECK(disp().substr(0,4)=="8000", "ADRS SET: [%s]", disp().c_str());
  static const char* prog[] = {"3E","5A","32","00","81","76"};     // MVI A,5Ah; STA 8100h; HLT
  for (const char* b : prog) { pressSeq85(b); press85("wr"); }
  CHECK(machine::read(0x8000)==0x3E && machine::read(0x8004)==0x81 && machine::read(0x8005)==0x76, "WR/ENT escribe y avanza");
  pressSeq85("8000"); press85("adrs"); press85("rinc");
  CHECK(disp().substr(0,4)=="8001", "READ INC: [%s]", disp().c_str());
  press85("rdec");
  CHECK(disp().substr(0,4)=="8000", "READ DEC: [%s]", disp().c_str());
  press85("run"); runCycles(300000);
  CHECK(machine::read(0x8100)==0x5A && machine::cpuPC()==0x8006, "RUN ejecuta el programa: [%02X] PC=%04X", machine::read(0x8100), machine::cpuPC());

  // MON (TRAP por hardware) vuelve al monitor aunque el programa esté parado con HLT
  press85("mon"); runCycles(600000);
  CHECK(machine::cpuPC() < 0x0800, "MON: PC=%04X", machine::cpuPC());

  // Programa en bucle (JMP $): MON lo interrumpe y CONT lo reanuda donde estaba
  pressSeq85("8000"); press85("adrs");
  static const char* loop[] = {"C3","00","80"};
  for (const char* b : loop) { pressSeq85(b); press85("wr"); }
  pressSeq85("8000"); press85("adrs"); press85("run"); runCycles(300000);
  CHECK(machine::cpuPC() >= 0x8000 && machine::cpuPC() <= 0x8002, "bucle en marcha: PC=%04X", machine::cpuPC());
  press85("mon"); runCycles(600000);
  CHECK(machine::cpuPC() < 0x0800, "MON saca del bucle: PC=%04X", machine::cpuPC());
  press85("cont"); runCycles(300000);
  CHECK(machine::cpuPC() >= 0x8000 && machine::cpuPC() <= 0x8002, "CONT reanuda el bucle: PC=%04X", machine::cpuPC());

  // Teclas propias de cada máquina
  CHECK(machine::setKey("mode", true) && machine::setKey("mode", false) && machine::setKey("reg", false) && machine::setKey("cont", false) && machine::setKey("mon", false) && !machine::setKey("winc", true) && !machine::setKey("store", true), "teclas tk85");
  CHECK(machine::setKey("reset", false), "reset siempre existe");

  // PROM de usuario: la CPU no la escribe, el cargador sí
  machine::write(0x0800, 0x11); CHECK(machine::read(0x0800)==0xFF, "PROM protegida para la CPU");
  machine::loadByte(0x0800, 0x55); CHECK(machine::read(0x0800)==0x55, "PROM cargable");

  // STORE/LOAD DATA no existen en el TK-85
  std::string body;
  CHECK(app::tapeStore(body)==400 && body.find("no tiene")!=std::string::npos, "tapeStore tk85: %s", body.c_str());

  // Cambio de máquina en caliente
  CHECK(machine::request("tk80") && !machine::request("zx81"), "request");
  CHECK(std::string(machine::target().id) == "tk80" && std::string(machine::spec().id) == "tk85", "target antes de aplicar");
  machine::pollReset();
  CHECK(std::string(machine::spec().id) == "tk80" && machine::read(0x0005) == 0x3B && machine::read(0x0800) == 0xFF, "pollReset aplica el cambio");
  runCycles(300000);
  CHECK(machine::display()[0] == 0x5C, "tk80 vuelve a arrancar con ceros de 5Ch");

  // JSON de la interfaz
  std::string j = machine::describeJson(*machine::find("tk85"));
  CHECK(j.find("\"id\":\"tk85\"") != std::string::npos && j.find("\"tape\":false") != std::string::npos &&
        j.find("\"k\":\"mode\"") != std::string::npos && j.find("{\"id\":\"tk80\"") != std::string::npos, "describeJson tk85");
  printf("describeJson(tk85): %zu bytes\n", j.size());
}

// ---------- TK-85: funciones de MODE (MOV, TM, IN, OUT) según el manual, p. 40-42 ----------
static void tk85ModeTests() {
  auto fresh = []() { machine::init("tk85"); runCycles(600000); };
  const uint8_t* d;

  // MOVE: copia 8000h-8050h a 8100h; muestra "8100 8150" y no toca el origen ni el byte siguiente al destino
  fresh();
  for (int i = 0; i <= 0x50; ++i) machine::write((uint16_t)(0x8000 + i), (uint8_t)(i * 3 + 1));
  press85("mode"); press85("d");
  d = machine::display();
  CHECK(d[0] == 0x80 && d[1] == 0 && d[2] == 0 && d[3] == 0 && d[4] == 0, "MODE D: solo el punto de MOVE en el primer dígito: [%02X %02X %02X]", d[0], d[1], d[2]);
  pressSeq85("8000"); press85("wr");
  pressSeq85("8050"); press85("wr");
  pressSeq85("8100"); press85("wr"); runCycles(3000000);
  int diff = 0; for (int i = 0; i <= 0x50; ++i) diff += machine::read((uint16_t)(0x8100 + i)) != machine::read((uint16_t)(0x8000 + i));
  CHECK(diff == 0 && machine::read(0x8151) == 0xFF, "MOVE copia el bloque (%d diferencias, 8151=%02X)", diff, machine::read(0x8151));
  CHECK(disp() == "8100 8150", "MOVE termina mostrando destino inicial y final: [%s]", disp().c_str());

  // MOVE con inicio > fin: error
  fresh(); press85("mode"); press85("d"); pressSeq85("8050"); press85("wr"); pressSeq85("8000"); press85("wr"); runCycles(1000000);
  d = machine::display();
  CHECK(d[1] == 0x79 && d[2] == 0x50 && d[3] == 0x50, "MOVE inicio > fin muestra Err: [%02X %02X %02X]", d[1], d[2], d[3]);

  // TEST MEMORY: RAM correcta -> "good" en el LED de datos (tarda como máximo unos 50M estados T)
  fresh(); press85("mode"); press85("c"); pressSeq85("8000"); press85("wr"); pressSeq85("8390");
  machine::setKey("wr", true); runCycles(400000); machine::setKey("wr", false);
  runCycles(60000000);
  d = machine::display();
  CHECK(d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 0 && d[4] == 0x6F && d[5] == 0x5C && d[6] == 0x5C && d[7] == 0x5E,
        "TEST MEMORY RAM: good [%02X %02X %02X %02X]", d[4], d[5], d[6], d[7]);

  // TEST MEMORY sobre la ROM: falla y muestra la dirección y escrito/leído (00 / 3E)
  fresh(); press85("mode"); press85("c"); pressSeq85("0000"); press85("wr"); pressSeq85("0010"); press85("wr"); runCycles(5000000);
  CHECK(disp() == "00.00 003E", "TEST MEMORY sobre ROM: error en 0000h con 00/3E: [%s]", disp().c_str());

  // IN: "1-" + puerto + WR/ENT -> "1-F8 dF" (con WR/ENT pulsada la línea 2 lee DFh, como en el manual)
  struct { const char* port; uint8_t hi, lo; } ins[] = { {"f8", 0x5E, 0x71}, {"f9", 0x71, 0x71}, {"20", 0x71, 0x71} };
  for (auto& in : ins) {
    fresh(); press85("mode"); press85("f");
    d = machine::display();
    CHECK(d[0] == 0x06 && d[1] == 0x40, "MODE F: prompt 1-");
    pressSeq85(in.port); press85("wr"); runCycles(300000);
    d = machine::display();
    CHECK(d[0] == 0x06 && d[1] == 0x40 && d[6] == in.hi && d[7] == in.lo, "IN %s: dato [%02X %02X]", in.port, d[6], d[7]);
    if (!strcmp(in.port, "f8")) { press85("wr"); d = machine::display(); CHECK(d[6] == 0x5E && d[7] == 0x71, "IN relee con WR/ENT"); }
  }

  // OUT: "0-" + puerto + WR/ENT + dato + WR/ENT -> el OUT llega al bus; otro dato + WR/ENT lo repite
  fresh(); press85("mode"); press85("e");
  d = machine::display();
  CHECK(d[0] == 0x3F && d[1] == 0x40, "MODE E: prompt 0-");
  pressSeq85("fc"); press85("wr");
  pressSeq85("21"); press85("wr"); runCycles(300000);
  uint8_t port = 0, val = 0;
  CHECK(machine::lastOut(port, val) && port == 0xFC && val == 0x21, "OUT FCh,21h: puerto=%02X valor=%02X", port, val);
  pressSeq85("5a"); press85("wr"); runCycles(300000);
  machine::lastOut(port, val);
  CHECK(port == 0xFC && val == 0x5A, "OUT repetido FCh,5Ah: puerto=%02X valor=%02X", port, val);
}

int main() {
  cpu85Tests();
  tk85Tests();
  tk85ModeTests();
  tapeTests();
  appTests();
  cpuTests();
  boardTests();
  hexTests();
  printf(failures ? "\n%d FALLOS\n" : "\nTODO OK\n", failures);
  return failures != 0;
}

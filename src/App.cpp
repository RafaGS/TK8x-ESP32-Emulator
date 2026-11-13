#include "App.h"
#include "Machine.h"
#include "IntelHex.h"
#include "config.h"
#include <atomic>
#include <stdio.h>

namespace app {

namespace {

// Máxima "deuda" de ciclos recuperable tras quedarse sin CPU: 50 ms. El presupuesto se
// mide en microsegundos x kHz (= estados T x 1000), así que depende del reloj de la máquina.
constexpr int64_t MAX_LAG_US = 50000;

// HEX ya validado a la espera de aplicarse (nullptr = ninguno). Lo escribe la
// tarea de red y lo consume pump(); la CPU solo se toca desde pump().
std::atomic<std::string*> pendingHex{nullptr};

void (*logFn)(const char*) = nullptr;
bool     started = false;
uint32_t lastUs = 0;
int64_t  budget = 0;

void logMsg(const char* m) { if (logFn) logFn(m); }

void applyPendingHex() {
  std::string* job = pendingHex.exchange(nullptr);
  if (!job) return;
  HexResult res;
  const bool ok = intelHexProcess(job->c_str(), job->size(), true, res);
  delete job;
  char msg[120];
  if (ok) snprintf(msg, sizeof msg, "Intel HEX loaded: %u data bytes, %u records, first address %04X",
                   (unsigned)res.bytes, (unsigned)res.records, (unsigned)res.firstAddr);
  else    snprintf(msg, sizeof msg, "HEX apply failed: %s", res.error);
  logMsg(msg);
}

}  // namespace

void init() {
  machine::init(DEFAULT_MACHINE);
  started = false;
  budget = 0;
}

void setLogger(void (*fn)(const char*)) { logFn = fn; }

void pump(uint32_t nowUs) {
  machine::pollReset();
  applyPendingHex();

  if (!started) { started = true; lastUs = nowUs; }
  const int64_t kHz = machine::spec().clockKHz;
  budget += (int64_t)(uint32_t)(nowUs - lastUs) * kHz;
  lastUs = nowUs;
  if (budget > MAX_LAG_US * kHz) budget = MAX_LAG_US * kHz;

  for (int i = 0; i < 2000 && budget > 0; ++i) {
    const int t = machine::step();
    if (t <= 0) { budget = 0; break; }       // RESET pulsado: CPU parada
    budget -= (int64_t)t * 1000;
  }
}

int displayJson(char* buf, size_t size) {
  const uint8_t* d = machine::display();
  return snprintf(buf, size, "{\"d\":[%u,%u,%u,%u,%u,%u,%u,%u]}",
                  d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
}

int tapeStore(std::string& body) {
  if (!machine::spec().tapeKeys) {
    body = "{\"ok\":false,\"error\":\"Esta máquina no tiene STORE DATA ni LOAD DATA\"}";
    return 400;
  }
  const uint16_t start = machine::readWord(machine::spec().monAdres);
  const uint16_t end   = machine::readWord(machine::spec().monData);
  char msg[160];
  if (end < start) {
    snprintf(msg, sizeof msg,
             "{\"ok\":false,\"error\":\"Rango no válido: la dirección final (%04X) es menor que la inicial (%04X)\"}",
             (unsigned)end, (unsigned)start);
    body = msg;
    return 400;
  }
  const uint32_t count = (uint32_t)end - start + 1;
  if (count > MAX_TAPE_BYTES) {
    snprintf(msg, sizeof msg, "{\"ok\":false,\"error\":\"Rango demasiado grande (%u bytes; máximo %u)\"}",
             (unsigned)count, (unsigned)MAX_TAPE_BYTES);
    body = msg;
    return 400;
  }

  // Intel HEX en registros de 16 bytes; los saltos de línea van escapados para el JSON.
  std::string hex;
  for (uint32_t off = 0; off < count; off += 16) {
    const uint8_t n = (uint8_t)((count - off) < 16 ? (count - off) : 16);
    const uint16_t addr = (uint16_t)(start + off);
    uint8_t sum = (uint8_t)(n + (addr >> 8) + (addr & 0xFF));
    char line[16];
    snprintf(line, sizeof line, ":%02X%04X00", n, (unsigned)addr);
    hex += line;
    for (uint8_t i = 0; i < n; ++i) {
      const uint8_t b = machine::read((uint16_t)(addr + i));
      sum = (uint8_t)(sum + b);
      snprintf(line, sizeof line, "%02X", b);
      hex += line;
    }
    snprintf(line, sizeof line, "%02X\\n", (unsigned)(uint8_t)(0x100 - sum));
    hex += line;
  }
  hex += ":00000001FF\\n";

  snprintf(msg, sizeof msg, "{\"ok\":true,\"start\":\"%04X\",\"end\":\"%04X\",\"bytes\":%u,\"hex\":\"",
           (unsigned)start, (unsigned)end, (unsigned)count);
  body = msg;
  body += hex;
  body += "\"}";
  return 200;
}

std::string machineJson() { return machine::describeJson(machine::target()); }

int selectMachine(const char* id, std::string& body) {
  if (!machine::request(id)) {
    body = "{\"ok\":false,\"error\":\"Máquina desconocida\"}";
    return 400;
  }
  body = machineJson();
  return 200;
}

int submitHex(std::string&& content, std::string& body) {
  if (content.size() > MAX_HEX_UPLOAD) {
    body = "{\"ok\":false,\"error\":\"Archivo demasiado grande (máximo 32 KiB)\"}";
    return 413;
  }
  HexResult res;
  // Se valida aquí (barato, sin tocar la CPU); se aplica luego en pump().
  if (!intelHexProcess(content.c_str(), content.size(), false, res)) {
    char msg[120]; snprintf(msg, sizeof msg, "HEX rejected: %s", res.error); logMsg(msg);
    body = std::string("{\"ok\":false,\"error\":\"") + res.error + "\"}";
    return 400;
  }
  std::string* job = new std::string(std::move(content));
  std::string* expected = nullptr;
  if (!pendingHex.compare_exchange_strong(expected, job)) {
    delete job;
    body = "{\"ok\":false,\"error\":\"Hay otra carga HEX en curso; inténtalo de nuevo\"}";
    return 409;
  }
  char buf[220];
  snprintf(buf, sizeof buf,
           "{\"ok\":true,\"bytes\":%u,\"records\":%u,\"start\":\"%04X\","
           "\"message\":\"HEX cargado. Teclea la dirección, pulsa ADRS SET y RUN.\"}",
           (unsigned)res.bytes, (unsigned)res.records, (unsigned)res.firstAddr);
  body = buf;
  return 200;
}

}  // namespace app

#ifdef HOST
#include "HostHttp.h"
#include "App.h"
#include "TK80Core.h"
#include "config.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>

static uint32_t nowMs() {
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
static bool     anyRequest = false;
static uint32_t lastRequestMs = 0;

uint32_t hostHttpIdleMs() { return anyRequest ? nowMs() - lastRequestMs : 0; }

static int         listenFd = -1;
static std::string rootDir = "data";

static const char* statusText(int code) {
  switch (code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 431: return "Request Header Fields Too Large";
    default:  return "Error";
  }
}

static void sendAll(int fd, const char* p, size_t n) {
  while (n) {
    ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
    if (w <= 0) return;
    p += w; n -= (size_t)w;
  }
}

static void respond(int fd, int code, const char* type, const std::string& body) {
  char head[256];
  int n = snprintf(head, sizeof head,
                   "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                   "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                   code, statusText(code), type, body.size());
  sendAll(fd, head, (size_t)n);
  sendAll(fd, body.data(), body.size());
}

static void respondJson(int fd, int code, const std::string& body) {
  respond(fd, code, "application/json; charset=utf-8", body);
}

// Valor de un parámetro de la query (?k=run&p=1). Los nombres de tecla son ASCII.
static std::string queryArg(const std::string& query, const char* name) {
  size_t pos = 0;
  const std::string key = std::string(name) + "=";
  while (pos <= query.size()) {
    size_t end = query.find('&', pos);
    if (end == std::string::npos) end = query.size();
    if (query.compare(pos, key.size(), key) == 0) return query.substr(pos + key.size(), end - pos - key.size());
    pos = end + 1;
  }
  return "";
}

static bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss; ss << f.rdbuf(); out = ss.str();
  return true;
}

static void handleConnection(int fd) {
  anyRequest = true;
  lastRequestMs = nowMs();
  // Lectura bloqueante pero acotada: un cliente lento no bloquea la emulación más de 300 ms.
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
  timeval tv{0, 300000};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

  std::string req;
  char buf[2048];
  size_t headEnd = std::string::npos;
  while (headEnd == std::string::npos) {
    ssize_t n = recv(fd, buf, sizeof buf, 0);
    if (n <= 0) return;
    req.append(buf, (size_t)n);
    headEnd = req.find("\r\n\r\n");
    if (headEnd == std::string::npos && req.size() > 16384) { respond(fd, 431, "text/plain", "Cabecera demasiado grande"); return; }
  }

  const size_t lineEnd = req.find("\r\n");
  const std::string line = req.substr(0, lineEnd);
  const size_t s1 = line.find(' '), s2 = line.find(' ', s1 + 1);
  if (s1 == std::string::npos || s2 == std::string::npos) { respond(fd, 400, "text/plain", "Petición no válida"); return; }
  const std::string method = line.substr(0, s1);
  std::string target = line.substr(s1 + 1, s2 - s1 - 1), query;
  const size_t q = target.find('?');
  if (q != std::string::npos) { query = target.substr(q + 1); target.resize(q); }

  // Content-Length (sin distinguir mayúsculas)
  size_t contentLength = 0;
  std::string headLower = req.substr(0, headEnd);
  std::transform(headLower.begin(), headLower.end(), headLower.begin(), [](unsigned char c) { return (char)tolower(c); });
  const size_t cl = headLower.find("\r\ncontent-length:");
  if (cl != std::string::npos) contentLength = (size_t)strtoul(headLower.c_str() + cl + 17, nullptr, 10);
  if (contentLength > MAX_HEX_UPLOAD) {
    respondJson(fd, 413, "{\"ok\":false,\"error\":\"Archivo demasiado grande (máximo 32 KiB)\"}");
    return;
  }
  std::string body = req.substr(headEnd + 4);
  while (body.size() < contentLength) {
    ssize_t n = recv(fd, buf, sizeof buf, 0);
    if (n <= 0) { respond(fd, 400, "text/plain", "Cuerpo incompleto"); return; }
    body.append(buf, (size_t)n);
  }
  body.resize(contentLength);

  if (method == "GET" && (target == "/" || target == "/index.html")) {
    std::string html;
    if (!readFile(rootDir + "/index.html", html)) {
      respond(fd, 404, "text/plain; charset=utf-8", "No se encuentra " + rootDir + "/index.html (define TK80_DATA con la ruta de la carpeta data)");
      return;
    }
    respond(fd, 200, "text/html; charset=utf-8", html);
  } else if (method == "GET" && target == "/display") {
    char json[80];
    int n = app::displayJson(json, sizeof json);
    respondJson(fd, 200, std::string(json, (size_t)n));
  } else if (method == "POST" && target == "/key") {
    const std::string k = queryArg(query, "k"), p = queryArg(query, "p");
    if (k.empty() || !tk80::setKey(k.c_str(), p == "1")) respondJson(fd, 400, "{\"ok\":false,\"error\":\"Tecla desconocida\"}");
    else respondJson(fd, 200, "{\"ok\":true}");
  } else if (method == "POST" && target == "/tape/store") {
    std::string out;
    int code = app::tapeStore(out);
    respondJson(fd, code, out);
  } else if (method == "POST" && target == "/hex/upload") {
    std::string out;
    int code = app::submitHex(std::move(body), out);
    respondJson(fd, code, out);
  } else {
    respond(fd, 404, "text/plain", "Not found");
  }
}

static bool hasIndex(const std::string& dir) {
  std::ifstream f(dir + "/index.html");
  return (bool)f;
}

// Busca data/index.html sin depender del directorio actual: primero el
// indicado (variable TK80_DATA o argumento), luego "data" en el directorio
// actual y por último relativo al ejecutable (.pio/build/native/program
// -> ../../../data, o una carpeta data junto al binario).
static std::string findDataDir(const char* preferred) {
  std::vector<std::string> candidates;
  if (const char* env = getenv("TK80_DATA")) candidates.push_back(env);
  if (preferred && *preferred) candidates.push_back(preferred);

  char exe[4096];
  ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
  if (n > 0) {
    exe[n] = 0;
    std::string dir(exe);
    dir.resize(dir.find_last_of('/') == std::string::npos ? 0 : dir.find_last_of('/'));
    candidates.push_back(dir + "/../../../data");
    candidates.push_back(dir + "/data");
    candidates.push_back(dir + "/../data");
  }
  for (const std::string& c : candidates)
    if (hasIndex(c)) return c;
  return preferred && *preferred ? preferred : "data";
}

const char* hostHttpRoot() { return rootDir.c_str(); }

bool hostHttpBegin(uint16_t port, const char* dataDir) {
  rootDir = findDataDir(dataDir);
  listenFd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd < 0) return false;
  int one = 1;
  setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);     // solo localhost
  addr.sin_port = htons(port);
  if (bind(listenFd, (sockaddr*)&addr, sizeof addr) < 0 || listen(listenFd, 8) < 0) {
    close(listenFd); listenFd = -1; return false;
  }
  fcntl(listenFd, F_SETFL, fcntl(listenFd, F_GETFL) | O_NONBLOCK);
  return true;
}

void hostHttpPoll() {
  if (listenFd < 0) return;
  for (int i = 0; i < 8; ++i) {
    int fd = accept(listenFd, nullptr, nullptr);
    if (fd < 0) return;                  // EAGAIN: no hay más conexiones pendientes
    handleConnection(fd);
    close(fd);
  }
}
#endif  // HOST

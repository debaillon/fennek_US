// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

#include "webfm.h"
#include "webfm_html.h"
#include "config.h"
#include "core/board.h"
#include "core/settings.h"
#include "core/display.h"
#include "core/gui.h"
#include "services/audio.h"
#include "services/battlog.h"
#include "services/ota.h"
#include "apps/mesh_client.h"
#include "services/wifi.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <GxEPD2_BW.h>   // GxEPD_BLACK / GxEPD_WHITE
#include <string.h>

namespace {

constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr size_t   kMaxPath = 192;      // FAT-Pfadkonvention des Projekts

webfm::State s_state = webfm::State::OFF;
WebServer*   s_server = nullptr;        // lazy — kostet erst bei Nutzung RAM
uint32_t     s_connectT0 = 0;
uint32_t     s_requests = 0;
char         s_ssid[33] = "";
char         s_ip[16] = "";
bool         s_rebootPending = false;   // nach erfolgreichem OTA: poll() rebootet

// Upload-Zustand (ein Upload zur Zeit — der Server ist synchron).
File s_upFile;
char s_upPath[kMaxPath] = "";
bool s_upOk = false;
int         s_upErrCode = 507;                     // HTTP-Code bei Fehlschlag
const char* s_upErr = "Upload failed";     // Response reason

void sendJsonErr(int code, const char* err) {
  char b[112];
  snprintf(b, sizeof(b), "{\"ok\":false,\"err\":\"%s\"}", err);
  s_server->send(code, "application/json", b);
}

void sendJsonOk() { s_server->send(200, "application/json", "{\"ok\":true}"); }

// "path"-Query-Parameter validieren. forWrite schützt die internen Caches
// (/.fennek) vor Lösch-/Schreibzugriffen — /meshcore ist bewusst NICHT
// geschützt (identity.hex-Upload ist ein vorgesehener Anwendungsfall).
// sendError=false für den Upload-Callback (dort darf nicht geantwortet werden).
bool argPath(char* out, size_t n, bool forWrite, bool sendError = true) {
  String p = s_server->arg("path");
  if (p.length() == 0 || p.length() >= n) {
    if (sendError) sendJsonErr(400, "Path is missing or too long");
    return false;
  }
  if (p[0] != '/' || p.indexOf("..") >= 0) {
    if (sendError) sendJsonErr(400, "Invalid path");
    return false;
  }
  if (forWrite && p.startsWith("/.fennek")) {
    if (sendError) sendJsonErr(403, "Protected path");
    return false;
  }
  strncpy(out, p.c_str(), n - 1);
  out[n - 1] = '\0';
  return true;
}

// Minimales JSON-Escaping für Dateinamen (Quote/Backslash; Steuerzeichen raus).
void appendJsonEscaped(String& json, const char* s) {
  for (; *s; s++) {
    if (*s == '"' || *s == '\\') { json += '\\'; json += *s; }
    else if ((uint8_t)*s >= 0x20) json += *s;
  }
}

// --- HTTP-Handler ------------------------------------------------------------
// Alle Handler laufen im UI-Thread (poll() -> handleClient()), also NIE unter
// bereits gehaltenem spiLock. Disziplin: SD nur unter dem Lock, Netz-I/O nur
// danach — bei großen Dateien gechunkt, damit E-Ink/Touch nicht verhungern.

void handleRoot() {
  s_requests++;
  s_server->send_P(200, "text/html", kWebFmHtml);
}

void handleList() {
  s_requests++;
  char p[kMaxPath];
  if (!argPath(p, sizeof(p), false)) return;
  if (!board::sdReady()) { sendJsonErr(503, "No SD card"); return; }

  String json;
  json.reserve(4096);
  json += "{\"path\":\"";
  appendJsonEscaped(json, p);
  json += "\",\"entries\":[";

  spiLock();
  File d = SD.open(p);
  if (!d || !d.isDirectory()) {
    if (d) d.close();
    spiUnlock();
    sendJsonErr(404, "Directory not found");
    return;
  }
  bool first = true;
  File f;
  while ((f = d.openNextFile())) {
    if (!first) json += ',';
    first = false;
    json += "{\"n\":\"";
    appendJsonEscaped(json, f.name());   // String-Ops = Heap, kein SPI — ok
    json += "\",\"s\":";
    json += (unsigned)f.size();
    json += ",\"d\":";
    json += f.isDirectory() ? "true" : "false";
    json += '}';
    f.close();
  }
  d.close();
  spiUnlock();

  json += "]}";
  s_server->send(200, "application/json", json);
}

void handleDownload() {
  s_requests++;
  char p[kMaxPath];
  if (!argPath(p, sizeof(p), false)) return;
  if (!board::sdReady()) { sendJsonErr(503, "No SD card"); return; }

  spiLock();
  File f = SD.open(p);
  bool ok = f && !f.isDirectory();
  uint32_t size = ok ? f.size() : 0;
  if (f && !ok) f.close();
  spiUnlock();
  if (!ok) { sendJsonErr(404, "File not found"); return; }

  const char* base = strrchr(p, '/');
  base = base ? base + 1 : p;
  char cd[224];
  snprintf(cd, sizeof(cd), "attachment; filename=\"%s\"", base);
  s_server->sendHeader("Content-Disposition", cd);
  s_server->setContentLength(size);
  s_server->send(200, "application/octet-stream", "");

  // Gechunkt streamen: SD-Read unter dem Lock, TCP-Send danach. Das Datei-
  // Handle zwischen den Chunks offen zu halten kostet keinen Buszugriff
  // (gleiches Muster wie der Audio-Read-Ahead).
  WiFiClient client = s_server->client();
  static uint8_t buf[4096];
  while (client.connected()) {
    spiLock();
    int rd = f.read(buf, sizeof(buf));
    spiUnlock();
    if (rd <= 0) break;
    size_t off = 0;
    while (off < (size_t)rd) {
      size_t wr = client.write(buf + off, rd - off);
      if (wr == 0) break;          // TCP-Fehler/Abbruch
      off += wr;
    }
    if (off < (size_t)rd) break;
  }
  spiLock();
  f.close();
  spiUnlock();
}

// Upload-Daten-Callback: wird vom WebServer pro Multipart-Häppchen (~1,4 KB)
// aufgerufen. Antworten sendet erst der Abschluss-Handler (handleUploadDone).
void handleUploadData() {
  HTTPUpload& up = s_server->upload();

  if (up.status == UPLOAD_FILE_START) {
    s_upOk = false;
    s_upPath[0] = '\0';
    s_upErrCode = 507;
    s_upErr = "Upload failed";
    char dir[kMaxPath];
    if (!argPath(dir, sizeof(dir), true, false)) {
      s_upErrCode = 403;
      s_upErr = "Destination path is invalid or protected (/.fennek)";
      return;
    }
    if (!board::sdReady()) {
      s_upErrCode = 503;
      s_upErr = "No SD card";
      return;
    }
    // Dateiname auf den Basename reduzieren (Browser können Pfade mitsenden).
    const char* name = up.filename.c_str();
    const char* slash = strrchr(name, '/');
    if (slash) name = slash + 1;
    if (!name[0]) { s_upErrCode = 400; s_upErr = "File name is missing"; return; }
    int n = snprintf(s_upPath, sizeof(s_upPath), "%s/%s",
                     (strcmp(dir, "/") == 0) ? "" : dir, name);
    if (n <= 0 || n >= (int)sizeof(s_upPath)) {
      s_upPath[0] = '\0';
      s_upErrCode = 400;
      s_upErr = "Path is too long";
      return;
    }
    spiLock();
    s_upFile = SD.open(s_upPath, FILE_WRITE);
    spiUnlock();
    if (s_upFile) {
      s_upOk = true;
      Serial.printf("[WEBFM] Upload started: %s\n", s_upPath);
    } else {
      s_upErr = "Could not create file (does the target folder exist?)";
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!s_upOk || !s_upFile) return;
    spiLock();
    size_t wr = s_upFile.write(up.buf, up.currentSize);
    spiUnlock();
    if (wr != up.currentSize) {     // SD voll/Schreibfehler -> abbrechen
      spiLock();
      s_upFile.close();
      SD.remove(s_upPath);
      spiUnlock();
      s_upOk = false;
      s_upErr = "Write error (SD full?)";
      Serial.printf("[WEBFM] Upload write error: %s\n", s_upPath);
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (s_upFile) {
      spiLock();
      s_upFile.close();
      spiUnlock();
      Serial.printf("[WEBFM] Upload complete: %s (%u bytes)\n",
                    s_upPath, (unsigned)up.totalSize);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (s_upFile) {
      spiLock();
      s_upFile.close();
      if (s_upPath[0]) SD.remove(s_upPath);
      spiUnlock();
    }
    s_upOk = false;
    Serial.println("[WEBFM] Upload canceled");
  }
}

void handleUploadDone() {
  s_requests++;
  if (s_upOk) sendJsonOk();
  else        sendJsonErr(s_upErrCode, s_upErr);
}

// Hat das Verzeichnis (eine Ebene) mindestens ein Unterverzeichnis? Caller hält
// spiLock. Entscheidet, ob das Löschen eine Rückfrage braucht.
bool hasSubdir(const char* path) {
  File d = SD.open(path);
  if (!d) return false;
  bool found = false;
  File c;
  while ((c = d.openNextFile())) {
    bool dir = c.isDirectory();
    c.close();
    if (dir) { found = true; break; }
  }
  d.close();
  return found;
}

// Löscht Datei oder Verzeichnis rekursiv. Caller hält spiLock (während des
// WLAN-Betriebs ist Audio eh gestoppt, s. webfm-Regel — langes Lock-Halten ok).
// Robust gegen Iterator-Invalidierung: das Verzeichnis wird pro Kind neu geöffnet
// und immer das erste Element gegriffen, statt während openNextFile() zu löschen.
bool rmRecursive(const char* path) {
  File f = SD.open(path);
  if (!f) return false;
  bool isDir = f.isDirectory();
  f.close();
  if (!isDir) return SD.remove(path);

  for (;;) {
    File d = SD.open(path);
    File c = d.openNextFile();
    if (!c) { d.close(); break; }
    char cp[kMaxPath];
    const char* sep = path[strlen(path) - 1] == '/' ? "" : "/";
    snprintf(cp, sizeof(cp), "%s%s%s", path, sep, c.name());
    bool childDir = c.isDirectory();
    c.close();
    d.close();
    if (!(childDir ? rmRecursive(cp) : SD.remove(cp))) return false;
  }
  return SD.rmdir(path);
}

void handleDelete() {
  s_requests++;
  char p[kMaxPath];
  if (!argPath(p, sizeof(p), true)) return;
  if (!board::sdReady()) { sendJsonErr(503, "No SD card"); return; }
  bool force = s_server->arg("force") == "1";

  spiLock();
  File f = SD.open(p);
  if (!f) {
    spiUnlock();
    sendJsonErr(404, "Not found");
    return;
  }
  bool isDir = f.isDirectory();
  f.close();

  // Ordner mit Unterordnern brauchen eine kurze Bestätigung (force=1); Dateien
  // und flache Ordner (leer oder nur Dateien) werden ohne Rückfrage gelöscht.
  if (isDir && !force && hasSubdir(p)) {
    spiUnlock();
    s_server->send(200, "application/json", "{\"ok\":false,\"confirm\":true}");
    return;
  }

  bool ok = rmRecursive(p);
  spiUnlock();

  if (ok) { Serial.printf("[WEBFM] Deleted: %s\n", p); sendJsonOk(); }
  else    sendJsonErr(500, "Delete failed");
}

void handleMkdir() {
  s_requests++;
  char p[kMaxPath];
  if (!argPath(p, sizeof(p), true)) return;
  if (!board::sdReady()) { sendJsonErr(503, "No SD card"); return; }

  spiLock();
  bool exists = SD.exists(p);
  bool ok = !exists && SD.mkdir(p);
  spiUnlock();

  if (ok) sendJsonOk();
  else if (exists) sendJsonErr(409, "Already exists");
  else             sendJsonErr(500, "mkdir failed");
}

// --- OTA-Firmware-Update -----------------------------------------------------
// WLAN ist hier bereits oben (Server läuft nur im Zustand RUNNING). Der lange,
// blockierende Flash ist unkritisch fürs SPI-Stottern: Audio ist gestoppt, die
// Mesh-Pumpe suspendiert, und OTA schreibt in den internen Flash (nicht HSPI).

// E-Ink-Hinweis während des Flashens (captureless für display::render).
void drawOtaScreen(Adafruit_GFX& g) {
  g.fillScreen(GxEPD_WHITE);
  gui::printAt(g, 16, 120, "Firmware-Update", 3);
  gui::printAt(g, 16, 160, "In progress - please wait", 2);
  gui::printAt(g, 16, 184, "Do NOT turn off.", 2);
  gui::printAt(g, 16, 230, "Restarting automatically.", 2);
}

// Netzfreier Versions-Endpunkt (für die Erstanzeige beim Seitenaufbau — kein
// blockierender GitHub-Aufruf bei jedem Laden der Dateiverwaltung).
void handleOtaVersion() {
  s_requests++;
  char b[48];
  snprintf(b, sizeof(b), "{\"current\":\"%s\"}", FENNEK_VERSION);
  s_server->send(200, "application/json", b);
}

void handleOtaCheck() {
  s_requests++;
  char url[160];
  settings::otaUrl(url, sizeof(url));
  ota::CheckResult r = ota::check(url);

  String json = "{\"ok\":";
  json += r.ok ? "true" : "false";
  json += ",\"current\":\"";  appendJsonEscaped(json, r.current);
  json += "\",\"latest\":\"";  appendJsonEscaped(json, r.latest);
  json += "\",\"update\":";   json += r.updateAvail ? "true" : "false";
  json += ",\"err\":\"";      appendJsonEscaped(json, r.err);
  json += "\"}";
  s_server->send(200, "application/json", json);
}

void handleOtaUpdate() {
  s_requests++;
  bool force = s_server->arg("force") == "1";

  char url[160];
  settings::otaUrl(url, sizeof(url));
  ota::CheckResult r = ota::check(url);
  if (!r.ok)                       { sendJsonErr(502, r.err); return; }
  if (!r.updateAvail && !force)    { sendJsonErr(409, "No update available"); return; }

  // Einmaliger Vollbild-Hinweis, dann blockierend flashen.
  display::render(drawOtaScreen, true);
  Serial.printf("[WEBFM] OTA update started (%s -> %s)\n", r.current, r.latest);

  char err[80] = "";
  if (ota::apply(r.url, err, sizeof(err))) {
    sendJsonOk();
    s_rebootPending = true;        // poll() rebootet, sobald die Antwort raus ist
  } else {
    sendJsonErr(500, err);
  }
}

void ensureServer() {
  if (s_server) return;
  s_server = new WebServer(80);
  s_server->on("/",             HTTP_GET,  handleRoot);
  s_server->on("/api/list",     HTTP_GET,  handleList);
  s_server->on("/api/download", HTTP_GET,  handleDownload);
  s_server->on("/api/upload",   HTTP_POST, handleUploadDone, handleUploadData);
  s_server->on("/api/delete",   HTTP_POST, handleDelete);
  s_server->on("/api/mkdir",    HTTP_POST, handleMkdir);
  s_server->on("/api/ota/version", HTTP_GET,  handleOtaVersion);
  s_server->on("/api/ota/check",   HTTP_GET,  handleOtaCheck);
  s_server->on("/api/ota/update",  HTTP_POST, handleOtaUpdate);
  s_server->onNotFound([]() { sendJsonErr(404, "unknown endpoint"); });
}

}  // namespace

namespace webfm {

bool start() {
  if (s_state == State::CONNECTING || s_state == State::RUNNING) return true;

  if (settings::wifiCount() == 0) {
    Serial.println("[WEBFM] No SSID configured ('wifi ssid <name>')");
    return false;
  }

  // Invarianten: WiFi-Task (Core 0, hohe Prio) verdrängt den Audio-Task,
  // und Dateitransfers brauchen den SPI-Bus — Audio aus, Mesh-Pumpe pausieren.
  audio::stop();
  mesh_client::setSuspended(true);

  // pickBest setzt STA-Mode + scannt und wählt das stärkste bekannte Netz;
  // wir behalten den bestehenden asynchronen Verbindungsaufbau (State-Machine).
  char pass[65];
  wifi::pickBest(s_ssid, sizeof(s_ssid), pass, sizeof(pass));
  WiFi.begin(s_ssid, pass);
  s_connectT0 = millis();
  s_requests = 0;
  s_ip[0] = '\0';
  s_state = State::CONNECTING;
  BATTLOG_EVENT("Wi-Fi", "on (%s)", s_ssid);   // Battery debug logger (no-op without BATTLOG)
  Serial.printf("[WEBFM] Connecting to '%s'... (free heap: %u KB)\n",
                s_ssid, (unsigned)(ESP.getFreeHeap() / 1024));
  return true;
}

void stop() {
  if (s_state == State::OFF) return;
  if (s_server) s_server->stop();
  MDNS.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  mesh_client::setSuspended(false);
  s_state = State::OFF;
  s_ip[0] = '\0';
  BATTLOG_EVENT("Wi-Fi", "off");   // Battery debug logger (no-op without BATTLOG)
  Serial.printf("[WEBFM] Wi-Fi off (free heap: %u KB)\n",
                (unsigned)(ESP.getFreeHeap() / 1024));
}

void poll() {
  if (s_state == State::CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setSleep(false);   // Modem-Sleep aus für vollen Datei-Transfer-Durchsatz
      snprintf(s_ip, sizeof(s_ip), "%s", WiFi.localIP().toString().c_str());
      ensureServer();
      s_server->begin();
      if (MDNS.begin("fennek")) MDNS.addService("http", "tcp", 80);
      s_state = State::RUNNING;
      Serial.printf("[WEBFM] Connected: http://%s/ (http://fennek.local/)\n", s_ip);
    } else if (millis() - s_connectT0 > kConnectTimeoutMs) {
      Serial.println("[WEBFM] Connection failed (timeout) — Wi-Fi off");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      mesh_client::setSuspended(false);
      s_state = State::FAILED;
    }
  } else if (s_state == State::RUNNING) {
    s_server->handleClient();
    if (s_rebootPending) {
      // Antwort ist raus (handleClient hat sie versandt) — jetzt neu starten.
      Serial.println("[WEBFM] OTA complete — rebooting into new version");
      delay(300);
      ESP.restart();
    }
  }
}

State state() { return s_state; }

void ipStr(char* out, size_t n) {
  strncpy(out, s_ip, n - 1);
  out[n - 1] = '\0';
}

void ssid(char* out, size_t n) {
  if (s_state == State::OFF) settings::wifiSsid(s_ssid, sizeof(s_ssid));
  strncpy(out, s_ssid, n - 1);
  out[n - 1] = '\0';
}

uint32_t requestCount() { return s_requests; }

}  // namespace webfm

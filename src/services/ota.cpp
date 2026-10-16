// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Dr. Daniel Dumke

#include "ota.h"
#include "config.h"
#include "services/audio.h"
#include "services/webfm.h"
#include "apps/mesh_client.h"
#include "core/settings.h"
#include "services/wifi.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

namespace {

constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint32_t kHttpTimeoutMs    = 15000;
constexpr uint32_t kReadTimeoutMs    = 8000;

// Active UI progress callback for deviceApply(), set only while flashing. The
// captureless Update.onProgress trampoline feeds this callback. It is null for
// console and web paths, which report to Serial instead.
ota::ProgressFn s_uiCb = nullptr;

// --- Wi-Fi connection helpers (scan and select the strongest known network) ---
// ssid/pass are ignored; wifi::connect selects from the saved profiles.
bool wifiUp(const char* ssid, const char* pass) {
  (void)ssid; (void)pass;
  return wifi::connect(kConnectTimeoutMs);
}

void wifiDown() { wifi::disconnect(); }

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Extract and unescape the JSON string value for "key" from body. This minimal
// parser does not use ArduinoJson and matches notes_ai::jsonExtractString.
bool jsonExtractString(const char* body, const char* key, char* out, size_t cap) {
  char needle[40];
  snprintf(needle, sizeof(needle), "\"%s\"", key);
  const char* p = strstr(body, needle);
  if (!p) return false;
  p += strlen(needle);
  while (*p == ' ' || *p == ':') p++;
  if (*p != '"') return false;
  p++;                                   // hinter das öffnende "
  size_t o = 0;
  while (*p && o + 2 < cap) {
    char c = *p++;
    if (c == '"') break;                 // unescaptes " = Stringende
    if (c == '\\') {
      char e = *p++;
      switch (e) {
        case 'n': out[o++] = '\n'; break;
        case 'r': out[o++] = '\r'; break;
        case 't': out[o++] = '\t'; break;
        case '/': out[o++] = '/';  break;
        case '"': out[o++] = '"';  break;
        case '\\': out[o++] = '\\'; break;
        case 'u': {
          int h0 = hexVal(p[0]), h1 = hexVal(p[1]), h2 = hexVal(p[2]), h3 = hexVal(p[3]);
          if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) { out[o] = '\0'; return o > 0; }
          uint32_t cp = (h0 << 12) | (h1 << 8) | (h2 << 4) | h3; p += 4;
          if (cp < 0x80) out[o++] = (char)cp;   // OTA-URLs sind reines ASCII
          break;
        }
        default: out[o++] = e; break;
      }
    } else {
      out[o++] = c;
    }
  }
  out[o] = '\0';
  return true;
}

// Find the first GitHub release browser_download_url ending in .bin.
bool findBinUrl(const char* body, char* out, size_t cap) {
  const char* key = "\"browser_download_url\"";
  for (const char* p = strstr(body, key); p; p = strstr(p + 1, key)) {
    char tmp[200];
    if (jsonExtractString(p, "browser_download_url", tmp, sizeof(tmp))) {
      size_t l = strlen(tmp);
      if (l >= 4 && strcasecmp(tmp + l - 4, ".bin") == 0) {
        strncpy(out, tmp, cap - 1);
        out[cap - 1] = '\0';
        return true;
      }
    }
  }
  return false;
}

// GET a response body into out in chunks, as in notes_ai. Returns true for
// HTTP 200 with a body.
bool httpGetBody(const char* url, char* out, size_t cap) {
  bool https = strncmp(url, "https", 5) == 0;
  HTTPClient http;
  WiFiClientSecure cs; WiFiClient cp;
  bool begun;
  if (https) { cs.setInsecure(); begun = http.begin(cs, url); }
  else       { begun = http.begin(cp, url); }
  if (!begun) return false;
  http.setUserAgent("fennek-ota");                       // GitHub verlangt User-Agent
  http.addHeader("Accept", "application/vnd.github+json");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kHttpTimeoutMs);
  int code = http.GET();
  if (code != 200) { Serial.printf("[OTA] Manifest HTTP %d\n", code); http.end(); return false; }

  WiFiClient* st = http.getStreamPtr();
  size_t total = 0; uint32_t last = millis();
  while (total < cap - 1) {
    int av = st->available();
    if (av > 0) {
      int want = (size_t)av < cap - 1 - total ? av : (int)(cap - 1 - total);
      int rd = st->readBytes((uint8_t*)out + total, want);
      if (rd > 0) { total += rd; last = millis(); }
    } else {
      if (!http.connected() && st->available() == 0) break;
      if (millis() - last > kReadTimeoutMs) break;
      delay(5);
    }
  }
  out[total] = '\0';
  http.end();
  return total > 0;
}

// Follow one redirect manually by reading the Location header without
// downloading the (potentially large) body. A direct 200 means the URL is final.
bool resolveRedirect(const char* url, char* out, size_t cap) {
  bool https = strncmp(url, "https", 5) == 0;
  HTTPClient http;
  WiFiClientSecure cs; WiFiClient cp;
  bool begun;
  if (https) { cs.setInsecure(); begun = http.begin(cs, url); }
  else       { begun = http.begin(cp, url); }
  if (!begun) return false;
  http.setUserAgent("fennek-ota");
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const char* hdrs[] = { "Location" };
  http.collectHeaders(hdrs, 1);
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kConnectTimeoutMs);
  int code = http.GET();
  bool ok = false;
  if (code >= 300 && code < 400 && http.hasHeader("Location")) {
    String loc = http.header("Location");
    strncpy(out, loc.c_str(), cap - 1);
    out[cap - 1] = '\0';
    ok = out[0] != '\0';
  } else if (code == 200) {
    strncpy(out, url, cap - 1);          // kein Redirect — URL ist schon final
    out[cap - 1] = '\0';
    ok = true;
  } else {
    Serial.printf("[OTA] Redirect-GET HTTP %d\n", code);
  }
  http.end();
  return ok;
}

// Compare semantic versions "vX.Y.Z". Returns true when a is newer than b.
bool semverGt(const char* a, const char* b) {
  const char* pa = (*a == 'v' || *a == 'V') ? a + 1 : a;
  const char* pb = (*b == 'v' || *b == 'V') ? b + 1 : b;
  int amaj = 0, amin = 0, apch = 0, bmaj = 0, bmin = 0, bpch = 0;
  sscanf(pa, "%d.%d.%d", &amaj, &amin, &apch);
  sscanf(pb, "%d.%d.%d", &bmaj, &bmin, &bpch);
  if (amaj != bmaj) return amaj > bmaj;
  if (amin != bmin) return amin > bmin;
  return apch > bpch;
}

// Report progress to Serial; captureless so it can be passed to Update.onProgress.
void otaProgress(size_t done, size_t total) {
  static int last = -1;
  int pct = total ? (int)(done * 100 / total) : 0;
  if (pct != last && (pct % 10 == 0 || pct == 100)) {
    Serial.printf("[OTA] %d%%\n", pct);
    if (s_uiCb) s_uiCb("Installing firmware", pct);   // E-Ink: 10% steps reduce ghosting
    last = pct;
  }
}

}  // namespace

namespace ota {

CheckResult check(const char* manifestUrl) {
  CheckResult r;
  snprintf(r.current, sizeof(r.current), "%s", FENNEK_VERSION);
  if (!manifestUrl || !manifestUrl[0]) {
    snprintf(r.err, sizeof(r.err), "No update URL configured");
    return r;
  }

  // GitHub-Release-JSON ist groß; tag_name + assets stehen vor den Release-Notes,
  // ein Abschnitt reicht. Heap-schonend einmalig holen.
  static char body[8192];
  if (!httpGetBody(manifestUrl, body, sizeof(body))) {
    snprintf(r.err, sizeof(r.err), "Manifest is unreachable");
    return r;
  }
  // Version: tag_name (GitHub) oder version (eigenes Manifest).
  if (!jsonExtractString(body, "tag_name", r.latest, sizeof(r.latest)) &&
      !jsonExtractString(body, "version",  r.latest, sizeof(r.latest))) {
    snprintf(r.err, sizeof(r.err), "No tag_name/version (is a release available?)");
    return r;
  }
  // Firmware-URL: erste .bin-Asset (GitHub) oder url (eigenes Manifest).
  if (!findBinUrl(body, r.url, sizeof(r.url)) &&
      (!jsonExtractString(body, "url", r.url, sizeof(r.url)) || !r.url[0])) {
    snprintf(r.err, sizeof(r.err), "No firmware.bin in release");
    return r;
  }
  r.ok = true;
  r.updateAvail = semverGt(r.latest, r.current);
  return r;
}

bool apply(const char* downloadUrl, char* errOut, size_t errCap) {
  auto fail = [&](const char* m) -> bool {
    if (errOut) { strncpy(errOut, m, errCap - 1); errOut[errCap - 1] = '\0'; }
    Serial.printf("[OTA] %s\n", m);
    return false;
  };
  if (!downloadUrl || !downloadUrl[0]) return fail("No firmware URL");

  // Resolve HTTP 302 redirects here (GitHub -> release-assets.githubusercontent.com)
  // to avoid httpUpdate's unreliable cross-host redirect handling. The signed
  // asset URL is long (~900 characters with X-Amz/signature query parameters),
  // so use a generous buffer to avoid truncation and a 4xx flash GET
  // (httpUpdate error -104).
  char finalUrl[1400];
  if (!resolveRedirect(downloadUrl, finalUrl, sizeof(finalUrl)))
    return fail("Redirect resolution failed");
  Serial.printf("[OTA] Downloading firmware: %s\n", finalUrl);

  bool https = strncmp(finalUrl, "https", 5) == 0;
  WiFiClientSecure cs; WiFiClient cp;
  WiFiClient* client;
  if (https) { cs.setInsecure(); client = &cs; } else client = &cp;

  httpUpdate.rebootOnUpdate(false);                       // The caller controls reboot
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  Update.onProgress(otaProgress);

  t_httpUpdate_return ret = httpUpdate.update(*client, finalUrl);
  if (ret == HTTP_UPDATE_OK) {
    Serial.println("[OTA] Update written — ready to reboot");
    return true;
  }
  char m[80];
  snprintf(m, sizeof(m), "Update error %d: %s", (int)httpUpdate.getLastError(),
           httpUpdate.getLastErrorString().c_str());
  return fail(m);
}

// --- Headless path (console) --------------------------------------------------
namespace {
void runConsole(bool doApply, bool force) {
  if (webfm::state() != webfm::State::OFF) {
    Serial.println("[OTA] Wi-Fi/WebFM is already running — stop it first");
    return;
  }
  char ssid[33], pass[65], url[160];
  settings::wifiSsid(ssid, sizeof(ssid));
  settings::wifiPass(pass, sizeof(pass));
  settings::otaUrl(url, sizeof(url));
  if (!ssid[0]) { Serial.println("[OTA] No Wi-Fi SSID configured ('wifi ssid <name>')"); return; }
  Serial.printf("[OTA] Connecting to '%s' ...\n", ssid);
  if (!wifiUp(ssid, pass)) { Serial.println("[OTA] Wi-Fi connection failed"); wifiDown(); return; }

  CheckResult r = check(url);
  if (!r.ok) { Serial.printf("[OTA] Check failed: %s\n", r.err); wifiDown(); return; }
  Serial.printf("[OTA] current=%s  latest=%s  %s\n", r.current, r.latest,
                r.updateAvail ? "(update available)" : "(up to date)");

  if (!doApply) { wifiDown(); return; }
  if (!r.updateAvail && !force) {
    Serial.println("[OTA] No update needed ('ota force' forces a reflash)");
    wifiDown();
    return;
  }

  char err[80] = "";
  if (apply(r.url, err, sizeof(err))) {
    Serial.println("[OTA] Rebooting into new version ...");
    delay(200);
    ESP.restart();
  } else {
    Serial.printf("[OTA] Failed: %s\n", err);
    wifiDown();
  }
}
}  // namespace

void consoleCheck()             { runConsole(false, false); }
void consoleUpdate(bool force)  { runConsole(true,  force); }

// --- On-device path (Settings app) -------------------------------------------
CheckResult deviceCheck(ProgressFn progress) {
  CheckResult r;
  snprintf(r.current, sizeof(r.current), "%s", FENNEK_VERSION);
  if (webfm::state() != webfm::State::OFF) {
    snprintf(r.err, sizeof(r.err), "Wi-Fi is busy (Files app?)");
    return r;
  }
  char ssid[33], pass[65], url[160];
  settings::wifiSsid(ssid, sizeof(ssid));
  settings::wifiPass(pass, sizeof(pass));
  settings::otaUrl(url, sizeof(url));
  if (!ssid[0]) { snprintf(r.err, sizeof(r.err), "No Wi-Fi SSID"); return r; }
  if (progress) progress("Connecting to Wi-Fi", -1);
  if (!wifiUp(ssid, pass)) {
    snprintf(r.err, sizeof(r.err), "Wi-Fi connection failed");
    wifiDown();
    return r;
  }

  if (progress) progress("Checking version", -1);
  r = check(url);          // Sets ok/err/current/latest/url/updateAvail
  wifiDown();
  return r;
}

bool deviceApply(const char* downloadUrl, ProgressFn progress, char* errOut, size_t errCap) {
  auto fail = [&](const char* m) -> bool {
    if (errOut) { strncpy(errOut, m, errCap - 1); errOut[errCap - 1] = '\0'; }
    return false;
  };
  if (!downloadUrl || !downloadUrl[0]) return fail("No firmware URL");
  if (webfm::state() != webfm::State::OFF) return fail("Wi-Fi is busy (Files app?)");

  char ssid[33], pass[65];
  settings::wifiSsid(ssid, sizeof(ssid));
  settings::wifiPass(pass, sizeof(pass));
  if (!ssid[0]) return fail("No Wi-Fi SSID");

  if (progress) progress("Connecting to Wi-Fi", -1);
  if (!wifiUp(ssid, pass)) { wifiDown(); return fail("Wi-Fi connection failed"); }

  if (progress) progress("Installing firmware", 0);
  s_uiCb = progress;                 // otaProgress() speist ab jetzt die UI
  char err[80] = "";
  bool ok = apply(downloadUrl, err, sizeof(err));
  s_uiCb = nullptr;
  if (!ok) { wifiDown(); return fail(err); }

  if (progress) progress("Neustart", 100);
  delay(800);
  ESP.restart();                     // kehrt nicht zurück
  return true;                       // unerreichbar
}

}  // namespace ota

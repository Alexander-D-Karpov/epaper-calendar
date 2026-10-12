#include "api.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <math.h>

#include "certs.h"
#include "config.h"
#include "text.h"
#include "timeutil.h"

namespace {

constexpr size_t kMaxBody = 16384;
constexpr size_t kBodyStep = 4096;
constexpr uint32_t kTimeoutMs = 15000;

BearSSL::X509List *gTrust = nullptr;
BearSSL::Session gSession;
int8_t gMfln = -1;

Tone toneFromHex(const char *hex) {
  if (!hex || hex[0] != '#' || strlen(hex) < 7)
    return {0, 32};
  const uint32_t v = strtoul(hex + 1, nullptr, 16);
  const int r = (v >> 16) & 0xFF;
  const int g = (v >> 8) & 0xFF;
  const int b = v & 0xFF;
  const int white = (g + b) * 64 / 510;
  int red = r * 64 / 255 - white;
  if (red < 0)
    red = 0;
  if (red > 64 - white)
    red = 64 - white;
  return {static_cast<uint8_t>(red), static_cast<uint8_t>(64 - white - red)};
}

void setupClient(BearSSL::WiFiClientSecure &c) {
  if (CAL_INSECURE) {
    c.setInsecure();
  } else {
    if (!gTrust)
      gTrust = new BearSSL::X509List(kRootCerts);
    c.setTrustAnchors(gTrust);
  }
  c.setSession(&gSession);
  if (gMfln < 0)
    gMfln = c.probeMaxFragmentLength(CAL_HOST, CAL_PORT, 1024) ? 1 : 0;
  if (gMfln == 1)
    c.setBufferSizes(1024, 1024);
  c.setTimeout(kTimeoutMs);
}

// Returns the Content-Length header, or -1 when the body runs to close.
long readHeaders(Stream &s) {
  long length = -1;
  while (true) {
    String line = s.readStringUntil('\n');
    line.trim();
    if (!line.length())
      return length;
    line.toLowerCase();
    if (line.startsWith("content-length:"))
      length = line.substring(15).toInt();
  }
}

// Reads the whole body so TLS can close before JSON parsing starts.
char *readBody(BearSSL::WiFiClientSecure &c, long length, size_t &n) {
  if (length > static_cast<long>(kMaxBody))
    return nullptr;
  size_t cap = length >= 0 ? static_cast<size_t>(length) : kBodyStep;
  char *buf = static_cast<char *>(malloc(cap + 1));
  if (!buf)
    return nullptr;
  n = 0;
  uint32_t last = millis();
  while (length < 0 || n < cap) {
    const int avail = c.available();
    if (avail <= 0) {
      if (!c.connected() || millis() - last > kTimeoutMs)
        break;
      delay(1);
      continue;
    }
    if (n == cap) {
      if (cap >= kMaxBody) {
        free(buf);
        return nullptr;
      }
      cap = cap + kBodyStep > kMaxBody ? kMaxBody : cap + kBodyStep;
      char *grown = static_cast<char *>(realloc(buf, cap + 1));
      if (!grown) {
        free(buf);
        return nullptr;
      }
      buf = grown;
    }
    const size_t room = cap - n;
    n += c.read(reinterpret_cast<uint8_t *>(buf + n),
                static_cast<size_t>(avail) < room ? avail : room);
    last = millis();
  }
  if (length >= 0 && n != static_cast<size_t>(length)) {
    free(buf);
    return nullptr;
  }
  buf[n] = '\0';
  return buf;
}

void fill(Snapshot &s, JsonObjectConst root, time_t now) {
  resetPool(s);
  copyUtf8(s.etag, sizeof(s.etag), root["etag"] | "");

  s.sleepCount = 0;
  for (JsonObjectConst w : root["sleep"].as<JsonArrayConst>()) {
    if (s.sleepCount >= kMaxSleep)
      break;
    s.sleep[s.sleepCount++] = {static_cast<uint8_t>(w["wd"] | 0),
                               static_cast<int16_t>(w["s"] | 0),
                               static_cast<int16_t>(w["e"] | 0)};
  }
  s.sleepEnabled = s.sleepCount > 0;

  JsonObjectConst w = root["weather"];
  s.weather = {};
  if (!w.isNull()) {
    s.weather.valid = true;
    s.weather.isDay = w["day"] | true;
    s.weather.code = w["code"] | 0;
    s.weather.tempX10 = static_cast<int16_t>(lroundf((w["t"] | 0.0f) * 10));
    s.weather.fetchedAt = w["at"] | 0;
  }

  s.eventCount = 0;
  for (JsonObjectConst o : root["events"].as<JsonArrayConst>()) {
    if (s.eventCount >= kMaxEvents)
      break;
    Event &e = s.events[s.eventCount++];
    e.start = o["s"] | 0;
    e.end = o["e"] | 0;
    e.tone = toneFromHex(o["c"] | "");
    e.flags = (o["all_day"] | false) ? kEventAllDay : 0;
    e.title = intern(s, o["title"] | "");
    e.place = intern(s, o["place"] | "");
  }

  s.todoCount = 0;
  for (JsonObjectConst o : root["todos"].as<JsonArrayConst>()) {
    if (s.todoCount >= kMaxTodos)
      break;
    int y, m, d;
    if (sscanf(o["due"] | "", "%4d-%2d-%2d", &y, &m, &d) != 3)
      continue;
    Todo &t = s.todos[s.todoCount++];
    t.dueDay = daysFromCivil(y, m, d);
    t.dueMin = o["time"].isNull() ? -1 : static_cast<int16_t>(o["time"] | 0);
    t.priority = o["prio"] | 0;
    t.checksDone = o["done"] | 0;
    t.checksTotal = o["total"] | 0;
    t.title = intern(s, o["title"] | "");
  }

  s.magic = kSnapshotMagic;
  s.version = kSnapshotVersion;
  s.size = sizeof(Snapshot);
  s.fetchedAt = static_cast<int32_t>(now);
  s.firstDay = localDay(now);
}

} // namespace

FetchResult apiFetchDisplay(Snapshot &s, bool conditional) {
  char path[128];
  snprintf(path, sizeof(path), "/api/v1/display?days=%d&lat=%.4f&lon=%.4f",
           cfg::kDisplayDays, static_cast<double>(GEO_LAT),
           static_cast<double>(GEO_LON));
  size_t n = 0;
  char *body = nullptr;
  {
    BearSSL::WiFiClientSecure client;
    setupClient(client);
    if (!client.connect(CAL_HOST, CAL_PORT)) {
      char err[80];
      client.getLastSSLError(err, sizeof(err));
      Serial.printf("api: connect failed: %s\n", err);
      return FetchResult::Failed;
    }
    client.print(F("GET "));
    client.print(path);
    client.print(F(" HTTP/1.0\r\nHost: " CAL_HOST
                   "\r\nAuthorization: Bearer " CAL_TOKEN
                   "\r\nAccept: application/json\r\nUser-Agent: "
                   "epaper-calendar/1.0\r\nConnection: close\r\n"));
    if (conditional && s.etag[0]) {
      client.print(F("If-None-Match: \""));
      client.print(s.etag);
      client.print(F("\"\r\n"));
    }
    client.print(F("\r\n"));
    const String status = client.readStringUntil('\n');
    const int sp = status.indexOf(' ');
    const int code = sp > 0 ? status.substring(sp + 1, sp + 4).toInt() : 0;
    if (code == 304)
      return FetchResult::NotModified;
    if (code != 200) {
      Serial.printf("api: display -> %d\n", code);
      return FetchResult::Failed;
    }
    body = readBody(client, readHeaders(client), n);
    if (!body) {
      Serial.printf("api: body read failed heap %u\n", ESP.getFreeHeap());
      return FetchResult::Failed;
    }
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body, n);
  if (err) {
    Serial.printf("api: json %s, %u bytes, heap %u\n", err.c_str(), n,
                  ESP.getFreeHeap());
    free(body);
    return FetchResult::Failed;
  }
  fill(s, doc.as<JsonObjectConst>(), time(nullptr));
  free(body);
  Serial.printf("api: %u events, %u todos, weather %d, %u bytes, heap %u\n",
                s.eventCount, s.todoCount, s.weather.valid, n,
                ESP.getFreeHeap());
  return FetchResult::Updated;
}

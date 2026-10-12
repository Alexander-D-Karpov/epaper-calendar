#include "api.h"

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <umm_malloc/umm_heap_select.h>

#include "certs.h"
#include "config.h"
#include "text.h"
#include "timeutil.h"

namespace {

BearSSL::X509List *gTrust = nullptr;
BearSSL::Session gSession;
int8_t gMfln = -1;

// TLS without MFLN leaves ~4 KB of DRAM, so JSON lives in the IRAM heap.
class IramAllocator : public ArduinoJson::Allocator {
public:
  void *allocate(size_t size) override {
    HeapSelectIram iram;
    return malloc(size);
  }
  void deallocate(void *ptr) override { free(ptr); }
  void *reallocate(void *ptr, size_t size) override {
    HeapSelectIram iram;
    return realloc(ptr, size);
  }
};

IramAllocator gJsonAlloc;

class Request {
public:
  ~Request() { client_.stop(); }

  bool get(const char *path) {
    if (CAL_INSECURE) {
      client_.setInsecure();
    } else {
      if (!gTrust)
        gTrust = new BearSSL::X509List(kRootCerts);
      client_.setTrustAnchors(gTrust);
    }
    client_.setSession(&gSession);
    if (gMfln < 0)
      gMfln = client_.probeMaxFragmentLength(CAL_HOST, CAL_PORT, 1024) ? 1 : 0;
    if (gMfln == 1)
      client_.setBufferSizes(1024, 1024);
    client_.setTimeout(15000);
    if (!client_.connect(CAL_HOST, CAL_PORT)) {
      char err[80];
      client_.getLastSSLError(err, sizeof(err));
      Serial.printf("api: connect failed: %s\n", err);
      return false;
    }
    client_.print(F("GET "));
    client_.print(path);
    client_.print(F(" HTTP/1.0\r\nHost: " CAL_HOST
                    "\r\nAuthorization: Bearer " CAL_TOKEN
                    "\r\nAccept: application/json\r\nUser-Agent: "
                    "epaper-calendar/1.0\r\nConnection: close\r\n\r\n"));
    const String status = client_.readStringUntil('\n');
    const int sp = status.indexOf(' ');
    const int code = sp > 0 ? status.substring(sp + 1, sp + 4).toInt() : 0;
    if (code != 200) {
      Serial.printf("api: %s -> %d\n", path, code);
      return false;
    }
    return client_.find("\r\n\r\n");
  }

  Stream &stream() { return client_; }

private:
  BearSSL::WiFiClientSecure client_;
};

bool skipSpaceUntil(Stream &s, char want) {
  const uint32_t start = millis();
  while (millis() - start < 15000) {
    const int c = s.peek();
    if (c < 0) {
      delay(1);
      continue;
    }
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
      s.read();
      continue;
    }
    return c == want;
  }
  return false;
}

template <typename F>
bool eachItem(Stream &s, const JsonDocument &filter, F fn) {
  if (!s.find("\"items\":["))
    return false;
  if (skipSpaceUntil(s, ']'))
    return true;
  JsonDocument doc(&gJsonAlloc);
  do {
    const DeserializationError err =
        deserializeJson(doc, s, DeserializationOption::Filter(filter));
    if (err) {
      Serial.printf("api: json %s heap %u\n", err.c_str(), ESP.getFreeHeap());
      return false;
    }
    fn(doc.as<JsonObjectConst>());
  } while (s.findUntil(",", "]"));
  return true;
}

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

int findCalendar(const Snapshot &s, const char *id) {
  for (uint8_t i = 0; i < s.calendarCount; i++) {
    if (!strcmp(s.calendars[i].id, id))
      return i;
  }
  return -1;
}

bool fetchMe(Snapshot &s) {
  Request req;
  if (!req.get("/api/v1/me"))
    return false;
  JsonDocument filter(&gJsonAlloc);
  filter["sleep"]["enabled"] = true;
  filter["sleep"]["windows"][0]["weekday"] = true;
  filter["sleep"]["windows"][0]["start"] = true;
  filter["sleep"]["windows"][0]["end"] = true;
  JsonDocument doc(&gJsonAlloc);
  if (deserializeJson(doc, req.stream(), DeserializationOption::Filter(filter)))
    return false;
  s.sleepEnabled = doc["sleep"]["enabled"] | false;
  s.sleepCount = 0;
  for (JsonObjectConst w : doc["sleep"]["windows"].as<JsonArrayConst>()) {
    if (s.sleepCount >= kMaxSleep)
      break;
    const int start = parseHm(w["start"] | "");
    const int end = parseHm(w["end"] | "");
    if (start < 0 || end < 0)
      continue;
    s.sleep[s.sleepCount++] = {static_cast<uint8_t>(w["weekday"] | 0),
                               static_cast<int16_t>(start),
                               static_cast<int16_t>(end)};
  }
  return true;
}

bool fetchCalendars(Snapshot &s) {
  Request req;
  if (!req.get("/api/v1/calendars"))
    return false;
  JsonDocument filter(&gJsonAlloc);
  filter["id"] = true;
  filter["color"] = true;
  filter["hidden"] = true;
  s.calendarCount = 0;
  return eachItem(req.stream(), filter, [&](JsonObjectConst o) {
    if (s.calendarCount >= kMaxCalendars || (o["hidden"] | false))
      return;
    Calendar &c = s.calendars[s.calendarCount++];
    copyUtf8(c.id, sizeof(c.id), o["id"] | "");
    c.tone = toneFromHex(o["color"] | "");
  });
}

bool fetchEvents(Snapshot &s, int32_t today) {
  char from[12];
  char to[12];
  char path[96];
  formatDay(today, from, sizeof(from));
  formatDay(today + cfg::kEventDays, to, sizeof(to));
  snprintf(path, sizeof(path), "/api/v1/events?from=%s&to=%s", from, to);
  Request req;
  if (!req.get(path))
    return false;
  JsonDocument filter(&gJsonAlloc);
  filter["calendar_id"] = true;
  filter["title"] = true;
  filter["location"] = true;
  filter["all_day"] = true;
  filter["start"] = true;
  filter["end"] = true;
  filter["status"] = true;
  s.eventCount = 0;
  return eachItem(req.stream(), filter, [&](JsonObjectConst o) {
    if (s.eventCount >= kMaxEvents)
      return;
    if (!strcmp(o["status"] | "", "cancelled"))
      return;
    const int cal = findCalendar(s, o["calendar_id"] | "");
    if (cal < 0)
      return;
    time_t start;
    time_t end;
    bool dateOnly;
    if (!parseIsoTime(o["start"] | "", start, dateOnly))
      return;
    const bool allDay = (o["all_day"] | false) || dateOnly;
    if (!parseIsoTime(o["end"] | "", end, dateOnly))
      end = start + (allDay ? 86400 : 3600);
    Event &e = s.events[s.eventCount++];
    e.start = static_cast<int32_t>(start);
    e.end = static_cast<int32_t>(end);
    e.cal = static_cast<uint8_t>(cal);
    e.flags = (allDay ? kEventAllDay : 0) |
              (!strcmp(o["status"] | "", "tentative") ? kEventTentative : 0);
    e.title = intern(s, o["title"] | "");
    e.place = intern(s, o["location"] | "");
  });
}

bool fetchTodos(Snapshot &s, int32_t today) {
  char from[12];
  char to[12];
  char path[128];
  formatDay(today - cfg::kOverdueDays, from, sizeof(from));
  formatDay(today + 1, to, sizeof(to));
  snprintf(path, sizeof(path),
           "/api/v1/todos?status=open&due_from=%s&due_to=%s&limit=100", from,
           to);
  Request req;
  if (!req.get(path))
    return false;
  JsonDocument filter(&gJsonAlloc);
  filter["title"] = true;
  filter["due_date"] = true;
  filter["due_time"] = true;
  filter["priority"] = true;
  filter["parent_id"] = true;
  filter["checks"][0]["done"] = true;
  s.todoCount = 0;
  return eachItem(req.stream(), filter, [&](JsonObjectConst o) {
    if (s.todoCount >= kMaxTodos || !o["parent_id"].isNull())
      return;
    const char *due = o["due_date"] | "";
    int y, m, d;
    if (sscanf(due, "%4d-%2d-%2d", &y, &m, &d) != 3)
      return;
    Todo &t = s.todos[s.todoCount++];
    t.dueDay = daysFromCivil(y, m, d);
    t.dueMin = static_cast<int16_t>(parseHm(o["due_time"] | ""));
    t.priority = o["priority"] | 0;
    t.checksDone = 0;
    t.checksTotal = 0;
    for (JsonObjectConst c : o["checks"].as<JsonArrayConst>()) {
      t.checksTotal++;
      if (c["done"] | false)
        t.checksDone++;
    }
    t.title = intern(s, o["title"] | "");
  });
}

} // namespace

bool apiLatestSeq(int64_t &latest) {
  Request req;
  if (!req.get("/api/v1/changes?since=0&limit=1"))
    return false;
  JsonDocument filter(&gJsonAlloc);
  filter["latest"] = true;
  JsonDocument doc(&gJsonAlloc);
  if (deserializeJson(doc, req.stream(), DeserializationOption::Filter(filter)))
    return false;
  if (doc["latest"].isNull())
    return false;
  latest = doc["latest"].as<int64_t>();
  return true;
}

bool apiFetchAll(Snapshot &s) {
  const time_t now = time(nullptr);
  const int32_t today = localDay(now);
  int64_t seq;
  if (!apiLatestSeq(seq))
    return false;
  resetPool(s);
  if (!fetchMe(s) || !fetchCalendars(s) || !fetchEvents(s, today) ||
      !fetchTodos(s, today))
    return false;
  s.magic = kSnapshotMagic;
  s.version = kSnapshotVersion;
  s.size = sizeof(Snapshot);
  s.seq = seq;
  s.fetchedAt = static_cast<int32_t>(now);
  s.firstDay = today;
  Serial.printf("api: %u calendars, %u events, %u todos, seq %lld, heap %u\n",
                s.calendarCount, s.eventCount, s.todoCount,
                static_cast<long long>(seq), ESP.getFreeHeap());
  return true;
}
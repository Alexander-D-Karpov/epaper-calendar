#pragma once
#include <stdint.h>
#include <time.h>

#include "weather.h"

constexpr uint8_t kMaxEvents = 40;
constexpr uint8_t kMaxTodos = 24;
constexpr uint8_t kMaxSleep = 14;
constexpr uint16_t kPoolSize = 4096;
constexpr uint8_t kMaxStrLen = 120;
constexpr uint32_t kSnapshotMagic = 0x45504331;
constexpr uint16_t kSnapshotVersion = 5;

struct Tone {
  uint8_t r;
  uint8_t k;
};

enum EventFlags : uint8_t {
  kEventAllDay = 1,
  kEventTentative = 2,
};

struct Event {
  int32_t start;
  int32_t end;
  Tone tone;
  uint8_t flags;
  uint16_t title;
  uint16_t place;
};

struct Todo {
  int32_t dueDay;
  int16_t dueMin;
  uint8_t priority;
  uint8_t checksDone;
  uint8_t checksTotal;
  uint16_t title;
};

struct SleepWindow {
  uint8_t weekday;
  int16_t start;
  int16_t end;
};

struct Snapshot {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  char etag[40];
  int32_t fetchedAt;
  int32_t firstDay;
  uint8_t eventCount;
  uint8_t todoCount;
  uint8_t sleepCount;
  bool sleepEnabled;
  uint16_t poolUsed;
  Event events[kMaxEvents];
  Todo todos[kMaxTodos];
  SleepWindow sleep[kMaxSleep];
  Weather weather;
  char pool[kPoolSize];
};

void resetPool(Snapshot &s);
uint16_t intern(Snapshot &s, const char *utf8);
const char *str(const Snapshot &s, uint16_t ref);
uint32_t snapshotHash(const Snapshot &s);

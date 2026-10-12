#pragma once
#include <stdint.h>
#include <time.h>

#include "model.h"

constexpr int kOverdueDays = 14;
constexpr uint8_t kMaxRest = 6;
constexpr uint8_t kMaxAllDay = 4;

struct Agenda {
  int32_t today;
  time_t dayStart;
  time_t dayEnd;
  int8_t card;
  bool cardCurrent;
  int8_t next;
  int8_t rest[kMaxRest];
  uint8_t restCount;
  int8_t allDay[kMaxAllDay];
  uint8_t allDayCount;
  int8_t timed[kMaxEvents];
  uint8_t timedCount;
  int8_t tomorrowFirst;
  uint8_t tomorrowCount;
  int8_t todos[kMaxTodos];
  uint8_t todoCount;
  uint8_t overdue;
  uint8_t dueToday;
};

struct SleepState {
  bool sleeping;
  time_t wakeAt;
  int tonightStart;
};

Agenda buildAgenda(const Snapshot &s, time_t now, bool timeValid);
SleepState sleepState(const Snapshot &s, time_t now);
time_t nextBoundary(const Snapshot &s, const Agenda &a, time_t now);
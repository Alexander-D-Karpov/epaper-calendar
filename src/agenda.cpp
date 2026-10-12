#include "agenda.h"

#include "timeutil.h"

namespace {

bool overlaps(const Event &e, time_t from, time_t to) {
  return e.start < to && e.end > from;
}

int todoRank(const Todo &t, int32_t today) {
  if (t.dueDay < today)
    return 0;
  return 1;
}

bool todoBefore(const Todo &a, const Todo &b, int32_t today) {
  const int ra = todoRank(a, today);
  const int rb = todoRank(b, today);
  if (ra != rb)
    return ra < rb;
  if (ra == 0) {
    if (a.priority != b.priority)
      return a.priority > b.priority;
    return a.dueDay < b.dueDay;
  }
  const int ma = a.dueMin < 0 ? 24 * 60 : a.dueMin;
  const int mb = b.dueMin < 0 ? 24 * 60 : b.dueMin;
  if (ma != mb)
    return ma < mb;
  return a.priority > b.priority;
}

} // namespace

Agenda buildAgenda(const Snapshot &s, time_t now, bool timeValid) {
  Agenda a = {};
  a.today = localDay(now);
  a.dayStart = localMidnight(a.today);
  a.dayEnd = localMidnight(a.today + 1);
  const time_t tomorrowEnd = localMidnight(a.today + 2);
  const time_t ref = timeValid ? now : a.dayStart;
  a.card = -1;
  a.next = -1;
  a.tomorrowFirst = -1;

  for (uint8_t i = 0; i < s.eventCount; i++) {
    const Event &e = s.events[i];
    const bool allDay = e.flags & kEventAllDay;
    if (overlaps(e, a.dayStart, a.dayEnd)) {
      if (allDay) {
        if (a.allDayCount < kMaxAllDay)
          a.allDay[a.allDayCount++] = i;
      } else {
        a.timed[a.timedCount++] = i;
      }
    }
    if (overlaps(e, a.dayEnd, tomorrowEnd)) {
      a.tomorrowCount++;
      if (!allDay && a.tomorrowFirst < 0 && e.start >= a.dayEnd)
        a.tomorrowFirst = i;
    }
  }

  for (uint8_t j = 0; j < a.timedCount; j++) {
    const Event &e = s.events[a.timed[j]];
    if (e.start <= ref && e.end > ref) {
      if (a.card < 0 || e.start >= s.events[a.card].start) {
        a.card = a.timed[j];
        a.cardCurrent = true;
      }
    }
  }
  for (uint8_t j = 0; j < a.timedCount; j++) {
    const int8_t idx = a.timed[j];
    const Event &e = s.events[idx];
    if (idx == a.card || e.start < ref)
      continue;
    if (a.card < 0) {
      a.card = idx;
      a.cardCurrent = false;
    } else if (a.next < 0) {
      a.next = idx;
    } else if (a.restCount < kMaxRest) {
      a.rest[a.restCount++] = idx;
    }
  }

  for (uint8_t i = 0; i < s.todoCount; i++) {
    const Todo &t = s.todos[i];
    if (t.dueDay > a.today || t.dueDay < a.today - kOverdueDays)
      continue;
    if (t.dueDay < a.today)
      a.overdue++;
    else
      a.dueToday++;
    uint8_t pos = a.todoCount++;
    while (pos > 0 && todoBefore(t, s.todos[a.todos[pos - 1]], a.today)) {
      a.todos[pos] = a.todos[pos - 1];
      pos--;
    }
    a.todos[pos] = i;
  }
  return a;
}

SleepState sleepState(const Snapshot &s, time_t now) {
  SleepState st = {false, 0, -1};
  if (!s.sleepEnabled)
    return st;
  const int32_t today = localDay(now);
  for (int off = -1; off <= 0; off++) {
    const int32_t day = today + off;
    const int wd = weekdayOf(day);
    for (uint8_t i = 0; i < s.sleepCount; i++) {
      const SleepWindow &w = s.sleep[i];
      if (w.weekday != wd)
        continue;
      const time_t start = localMidnight(day) + w.start * 60;
      const time_t end = w.end > w.start ? localMidnight(day) + w.end * 60
                                         : localMidnight(day + 1) + w.end * 60;
      if (off == 0)
        st.tonightStart = w.start;
      if (now >= start && now < end) {
        st.sleeping = true;
        st.wakeAt = end;
      }
    }
  }
  return st;
}

time_t nextBoundary(const Snapshot &s, const Agenda &a, time_t now) {
  time_t best = a.dayEnd;
  for (uint8_t j = 0; j < a.timedCount; j++) {
    const Event &e = s.events[a.timed[j]];
    if (e.start > now && e.start < best)
      best = e.start;
    if (e.end > now && e.end < best)
      best = e.end;
  }
  return best;
}
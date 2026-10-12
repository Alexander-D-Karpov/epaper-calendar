#include "model.h"

#include <string.h>

void resetPool(Snapshot &s) {
  s.pool[0] = '\0';
  s.poolUsed = 1;
}

uint16_t intern(Snapshot &s, const char *utf8) {
  if (!utf8 || !*utf8 || s.poolUsed >= kPoolSize - 1)
    return 0;
  size_t n = strlen(utf8);
  const size_t room = kPoolSize - 1 - s.poolUsed;
  if (n > kMaxStrLen)
    n = kMaxStrLen;
  if (n > room)
    n = room;
  while (n > 0 && (static_cast<uint8_t>(utf8[n]) & 0xC0) == 0x80)
    n--;
  if (!n)
    return 0;
  const uint16_t ref = s.poolUsed;
  memcpy(s.pool + ref, utf8, n);
  s.pool[ref + n] = '\0';
  s.poolUsed = static_cast<uint16_t>(ref + n + 1);
  return ref;
}

const char *str(const Snapshot &s, uint16_t ref) {
  return ref < s.poolUsed ? s.pool + ref : "";
}

uint32_t snapshotHash(const Snapshot &s) {
  uint32_t h = 2166136261u;
  auto mix = [&h](const void *p, size_t n) {
    const uint8_t *b = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i < n; i++) {
      h ^= b[i];
      h *= 16777619u;
    }
  };
  // Weather is left out: it changes often and has its own live region.
  mix(&s.firstDay, sizeof(s.firstDay));
  mix(s.events, sizeof(Event) * s.eventCount);
  mix(s.todos, sizeof(Todo) * s.todoCount);
  mix(s.sleep, sizeof(SleepWindow) * s.sleepCount);
  mix(s.pool, s.poolUsed);
  return h;
}

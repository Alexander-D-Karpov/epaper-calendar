#include "timeutil.h"

#include <stdio.h>
#include <string.h>

int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

void civilFromDays(int32_t z, int &y, int &m, int &d) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  y = static_cast<int>(yoe) + era * 400 + (m <= 2);
}

int32_t localDay(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  return daysFromCivil(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

int localMinute(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  return tm.tm_hour * 60 + tm.tm_min;
}

int weekdayOf(int32_t day) {
  int w = static_cast<int>((day + 4) % 7);
  return w < 0 ? w + 7 : w;
}

time_t localMidnight(int32_t day) {
  int y, m, d;
  civilFromDays(day, y, m, d);
  struct tm tm = {};
  tm.tm_year = y - 1900;
  tm.tm_mon = m - 1;
  tm.tm_mday = d;
  tm.tm_isdst = -1;
  return mktime(&tm);
}

static bool digits(const char *s, int n, int &out) {
  out = 0;
  for (int i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9')
      return false;
    out = out * 10 + (s[i] - '0');
  }
  return true;
}

bool parseIsoTime(const char *s, time_t &out, bool &dateOnly) {
  int y, mo, d;
  if (!s || strlen(s) < 10)
    return false;
  if (!digits(s, 4, y) || s[4] != '-' || !digits(s + 5, 2, mo) || s[7] != '-' ||
      !digits(s + 8, 2, d))
    return false;
  const int32_t day = daysFromCivil(y, mo, d);
  if (s[10] == '\0') {
    dateOnly = true;
    out = localMidnight(day);
    return true;
  }
  int h, mi, se = 0;
  if (s[10] != 'T' || !digits(s + 11, 2, h) || s[13] != ':' ||
      !digits(s + 14, 2, mi))
    return false;
  const char *p = s + 16;
  if (*p == ':') {
    if (!digits(p + 1, 2, se))
      return false;
    p += 3;
  }
  if (*p == '.') {
    p++;
    while (*p >= '0' && *p <= '9')
      p++;
  }
  int32_t offset = 0;
  if (*p == '+' || *p == '-') {
    int oh, om;
    if (!digits(p + 1, 2, oh) || p[3] != ':' || !digits(p + 4, 2, om))
      return false;
    offset = (oh * 3600 + om * 60) * (*p == '-' ? -1 : 1);
  } else if (*p != 'Z') {
    return false;
  }
  dateOnly = false;
  out = static_cast<time_t>(day) * 86400 + h * 3600 + mi * 60 + se - offset;
  return true;
}

int parseHm(const char *s) {
  int h, m;
  if (!s || strlen(s) < 5 || !digits(s, 2, h) || s[2] != ':' ||
      !digits(s + 3, 2, m))
    return -1;
  return h * 60 + m;
}

void formatDay(int32_t day, char *out, size_t cap) {
  int y, m, d;
  civilFromDays(day, y, m, d);
  snprintf(out, cap, "%04d-%02d-%02d", y, m, d);
}

void formatHm(time_t t, char *out, size_t cap) {
  formatMinutes(localMinute(t), out, cap);
}

void formatMinutes(int minutes, char *out, size_t cap) {
  snprintf(out, cap, "%02d:%02d", (minutes / 60) % 24, minutes % 60);
}

void formatDuration(int32_t seconds, char *out, size_t cap) {
  const int32_t minutes = (seconds + 59) / 60;
  if (minutes < 60) {
    snprintf(out, cap, "%d мин", static_cast<int>(minutes));
  } else {
    snprintf(out, cap, "%d:%02d", static_cast<int>(minutes / 60),
             static_cast<int>(minutes % 60));
  }
}
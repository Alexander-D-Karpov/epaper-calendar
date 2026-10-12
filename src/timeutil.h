#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

int32_t daysFromCivil(int y, int m, int d);
void civilFromDays(int32_t z, int &y, int &m, int &d);
int32_t localDay(time_t t);
int localMinute(time_t t);
int weekdayOf(int32_t day);
time_t localMidnight(int32_t day);
bool parseIsoTime(const char *s, time_t &out, bool &dateOnly);
int parseHm(const char *s);
void formatDay(int32_t day, char *out, size_t cap);
void formatHm(time_t t, char *out, size_t cap);
void formatMinutes(int minutes, char *out, size_t cap);
void formatDuration(int32_t seconds, char *out, size_t cap);
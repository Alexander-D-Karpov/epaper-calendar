#pragma once
#include <stdint.h>
#include <time.h>

enum class Phase : uint8_t { Morning, Day, Evening, Night };

struct SunPos {
  float elevation;
  float azimuth;
};

struct SunDay {
  time_t sunrise;
  time_t sunset;
  time_t noon;
  float maxElevation;
  bool rises;
};

SunPos sunPosition(time_t utc, double lat, double lon);
SunDay sunDay(time_t midnight, double lat, double lon);
Phase sunPhase(time_t now, const SunDay& day, const SunPos& pos);
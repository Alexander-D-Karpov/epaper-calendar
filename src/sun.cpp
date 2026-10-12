#include "sun.h"

#include <math.h>

namespace {
constexpr double kRad = M_PI / 180.0;
constexpr double kDeg = 180.0 / M_PI;
constexpr float kHorizon = -0.833f;
constexpr float kNight = -6.0f;
} // namespace

SunPos sunPosition(time_t utc, double lat, double lon) {
  const double jd = static_cast<double>(utc) / 86400.0 + 2440587.5;
  const double t = (jd - 2451545.0) / 36525.0;
  const double l0 = fmod(280.46646 + t * (36000.76983 + t * 0.0003032), 360.0);
  const double m = 357.52911 + t * (35999.05029 - 0.0001537 * t);
  const double e = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
  const double c = sin(m * kRad) * (1.914602 - t * (0.004817 + 0.000014 * t)) +
                   sin(2 * m * kRad) * (0.019993 - 0.000101 * t) +
                   sin(3 * m * kRad) * 0.000289;
  const double omega = 125.04 - 1934.136 * t;
  const double lambda = l0 + c - 0.00569 - 0.00478 * sin(omega * kRad);
  const double eps0 =
      23.0 +
      (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) /
          60.0;
  const double eps = eps0 + 0.00256 * cos(omega * kRad);
  const double decl = asin(sin(eps * kRad) * sin(lambda * kRad));
  const double y = pow(tan(eps * kRad / 2), 2);
  const double eqTime =
      4 * kDeg *
      (y * sin(2 * l0 * kRad) - 2 * e * sin(m * kRad) +
       4 * e * y * sin(m * kRad) * cos(2 * l0 * kRad) -
       0.5 * y * y * sin(4 * l0 * kRad) - 1.25 * e * e * sin(2 * m * kRad));
  const double minutes = fmod(static_cast<double>(utc), 86400.0) / 60.0;
  double solar = fmod(minutes + eqTime + 4 * lon, 1440.0);
  if (solar < 0)
    solar += 1440.0;
  double ha = solar / 4.0 - 180.0;
  const double latR = lat * kRad;
  const double haR = ha * kRad;
  double cz = sin(latR) * sin(decl) + cos(latR) * cos(decl) * cos(haR);
  if (cz > 1)
    cz = 1;
  if (cz < -1)
    cz = -1;
  const double elevation = 90.0 - acos(cz) * kDeg;
  double az =
      atan2(sin(haR), cos(haR) * sin(latR) - tan(decl) * cos(latR)) * kDeg +
      180.0;
  if (az >= 360)
    az -= 360;
  return {static_cast<float>(elevation), static_cast<float>(az)};
}

SunDay sunDay(time_t midnight, double lat, double lon) {
  SunDay day = {0, 0, midnight + 43200, -90, false};
  constexpr int kStep = 300;
  float prev = sunPosition(midnight, lat, lon).elevation;
  for (int s = kStep; s <= 86400; s += kStep) {
    const time_t t = midnight + s;
    const float cur = sunPosition(t, lat, lon).elevation;
    if (cur > day.maxElevation) {
      day.maxElevation = cur;
      day.noon = t;
    }
    if (prev < kHorizon && cur >= kHorizon && !day.sunrise) {
      day.sunrise =
          t - static_cast<time_t>(kStep * (cur - kHorizon) / (cur - prev));
    }
    if (prev >= kHorizon && cur < kHorizon && !day.sunset) {
      day.sunset =
          t - static_cast<time_t>(kStep * (cur - kHorizon) / (cur - prev));
    }
    prev = cur;
  }
  day.rises = day.sunrise && day.sunset;
  return day;
}

Phase sunPhase(time_t now, const SunDay &day, const SunPos &pos) {
  if (pos.elevation < kNight)
    return Phase::Night;
  const float threshold = day.maxElevation > 0 ? day.maxElevation * 0.5f : 0;
  if (pos.elevation >= threshold && pos.elevation > 0)
    return Phase::Day;
  return now < day.noon ? Phase::Morning : Phase::Evening;
}
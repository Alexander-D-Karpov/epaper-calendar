#pragma once
#include <Adafruit_GFX.h>
#include <time.h>

#include "agenda.h"
#include "model.h"
#include "sun.h"
#include "weather.h"

constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kRed = 0xF800;

struct View {
  const Snapshot *snap;
  time_t now;
  bool timeValid;
  bool online;
  double lat;
  double lon;
  SunPos sun;
  SunDay sunDay;
  Phase phase;
  Agenda agenda;
  SleepState sleep;
  Weather weather;
};

struct Rect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

constexpr uint8_t kLiveRegionCount = 5;
constexpr size_t kLiveRegionBytes =
    (284 * 24 + 164 * 24 + 300 * 8 + 60 * 16 + 184 * 24) / 8;
extern const Rect kLiveRegions[kLiveRegionCount];

View makeView(const Snapshot &s, time_t now, bool timeValid, bool online,
              double lat, double lon, const Weather &w);
uint8_t skyKind(const View &v);
void drawWeatherIcon(Adafruit_GFX &g, int16_t x, int16_t y, uint8_t code,
                     bool isDay, uint16_t fg, uint16_t bg);
void renderDay(Adafruit_GFX &g, const View &v);
void renderSleep(Adafruit_GFX &g, const View &v);
void renderMessage(Adafruit_GFX &g, const char *title, const char *body);
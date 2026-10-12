#pragma once
#include <Adafruit_GFX.h>
#include <time.h>

#include "agenda.h"
#include "model.h"
#include "sun.h"

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
};

struct Rect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

constexpr uint8_t kLiveRegionCount = 4;
extern const Rect kLiveRegions[kLiveRegionCount];

View makeView(const Snapshot &s, time_t now, bool timeValid, bool online,
              double lat, double lon);
void renderDay(Adafruit_GFX &g, const View &v);
void renderSleep(Adafruit_GFX &g, const View &v);
void renderMessage(Adafruit_GFX &g, const char *title, const char *body);
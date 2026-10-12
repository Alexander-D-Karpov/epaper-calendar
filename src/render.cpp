#include "render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fonts_gen.h"
#include "text.h"
#include "timeutil.h"

namespace {

constexpr int16_t kW = 300;
constexpr int16_t kH = 400;
constexpr int16_t kM = 8;
constexpr int16_t kSkyX = 186;
constexpr int16_t kSkyH = 70;
constexpr int16_t kCardY = 80;
constexpr int16_t kCardH = 80;
constexpr int16_t kBandY = 136;
constexpr int16_t kNextY = 166;
constexpr int16_t kStripY = 208;
constexpr int16_t kStripH = 12;
constexpr int16_t kListY = 240;
constexpr int16_t kListEnd = 300;
constexpr int16_t kListRowH = 15;
constexpr int16_t kTodoY = 302;
constexpr int16_t kTodoRowsY = 326;
constexpr int16_t kTodoRowH = 14;
constexpr int16_t kFooterY = 384;
constexpr int16_t kClockX = 240;

const uint8_t kBayer[8][8] = {
    {0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4, 36, 14, 46, 6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
    {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37}, {63, 31, 55, 23, 61, 29, 53, 21},
};

const char *const kWeekdays[] = {"воскресенье", "понедельник", "вторник",
                                 "среда",       "четверг",     "пятница",
                                 "суббота"};
const char *const kMonths[] = {"январь",   "февраль", "март",   "апрель",
                               "май",      "июнь",    "июль",   "август",
                               "сентябрь", "октябрь", "ноябрь", "декабрь"};
const char *const kMonthsGen[] = {"января",   "февраля", "марта",  "апреля",
                                  "мая",      "июня",    "июля",   "августа",
                                  "сентября", "октября", "ноября", "декабря"};

struct Theme {
  bool dark;
  uint16_t bg;
  uint16_t fg;
  uint16_t cardBg;
  uint16_t cardFg;
  uint16_t pathDot;
  Tone track;
  Tone sky[3];
  Tone hillsBack;
  Tone hillsFront;
};

Theme themeFor(Phase p) {
  switch (p) {
  case Phase::Morning:
    return {false,   kWhite, kBlack,  kRed,
            kWhite,  kBlack, {0, 12}, {{0, 8}, {6, 0}, {30, 0}},
            {0, 18}, {0, 30}};
  case Phase::Evening:
    return {false,    kWhite,  kBlack,  kBlack,
            kWhite,   kWhite,  {0, 16}, {{0, 34}, {26, 6}, {52, 10}},
            {20, 36}, {12, 52}};
  case Phase::Night:
    return {true,    kBlack, kWhite,  kRed,
            kWhite,  kWhite, {0, 46}, {{0, 64}, {0, 58}, {0, 44}},
            {0, 30}, {0, 46}};
  case Phase::Day:
  default:
    return {false,   kWhite, kBlack,  kRed,
            kWhite,  kBlack, {0, 12}, {{0, 24}, {0, 8}, {0, 0}},
            {0, 26}, {0, 40}};
  }
}

uint16_t tonePixel(Tone t, int16_t x, int16_t y) {
  const uint8_t th = kBayer[y & 7][x & 7];
  if (th < t.r)
    return kRed;
  if (th < t.r + t.k)
    return kBlack;
  return kWhite;
}

void fillTone(Adafruit_GFX &g, int16_t x, int16_t y, int16_t w, int16_t h,
              Tone t) {
  if (w <= 0 || h <= 0)
    return;
  if (t.r == 0 && t.k == 0)
    return g.fillRect(x, y, w, h, kWhite);
  if (t.r >= 64)
    return g.fillRect(x, y, w, h, kRed);
  if (t.k >= 64)
    return g.fillRect(x, y, w, h, kBlack);
  for (int16_t yy = y; yy < y + h; yy++) {
    for (int16_t xx = x; xx < x + w; xx++)
      g.drawPixel(xx, yy, tonePixel(t, xx, yy));
  }
}

Tone lerp(Tone a, Tone b, int32_t i, int32_t n) {
  if (n <= 0)
    return a;
  return {
      static_cast<uint8_t>(a.r + (static_cast<int32_t>(b.r) - a.r) * i / n),
      static_cast<uint8_t>(a.k + (static_cast<int32_t>(b.k) - a.k) * i / n)};
}

void gradV(Adafruit_GFX &g, int16_t x, int16_t y, int16_t w, int16_t h,
           const Tone *stops, uint8_t n) {
  const int32_t span = (h - 1) * (n - 1);
  for (int16_t row = 0; row < h; row++) {
    const int32_t pos =
        span ? static_cast<int32_t>(row) * (n - 1) * 1000 / (h - 1) : 0;
    const uint8_t seg =
        static_cast<uint8_t>(pos / 1000 >= n - 1 ? n - 2 : pos / 1000);
    fillTone(g, x, y + row, w, 1,
             lerp(stops[seg], stops[seg + 1], pos - seg * 1000, 1000));
  }
}

Tone themed(Tone t, const Theme &th) {
  if (!th.dark)
    return t;
  const uint8_t white = 64 - t.r - t.k;
  return {t.r, white};
}

Tone faded(Tone t, const Theme &th) {
  if (!th.dark)
    return {static_cast<uint8_t>(t.r * 45 / 100),
            static_cast<uint8_t>(t.k * 45 / 100)};
  const uint8_t white = 64 - t.r - t.k;
  const uint8_t r = t.r * 45 / 100;
  const uint8_t w = white * 45 / 100;
  return {r, static_cast<uint8_t>(64 - r - w)};
}

const char *plural(int n, const char *one, const char *few, const char *many) {
  n = n % 100;
  if (n >= 11 && n <= 14)
    return many;
  switch (n % 10) {
  case 1:
    return one;
  case 2:
  case 3:
  case 4:
    return few;
  default:
    return many;
  }
}

Tone eventTone(const View &v, const Event &e, const Theme &th) {
  return themed(v.snap->calendars[e.cal].tone, th);
}

void hills(Adafruit_GFX &g, int16_t x0, int16_t w, int16_t base, int16_t bottom,
           float phase, float amp, Tone t) {
  for (int16_t i = 0; i < w; i++) {
    const float h = amp * (0.55f * sinf(i * 0.075f + phase) +
                           0.35f * sinf(i * 0.19f + phase * 2.3f) +
                           0.1f * sinf(i * 0.47f + phase * 0.7f));
    const int16_t top = base - static_cast<int16_t>(h);
    fillTone(g, x0 + i, top, 1, bottom - top, t);
  }
}

void stars(Adafruit_GFX &g, int16_t x0, int16_t y0, int16_t w, int16_t h,
           uint32_t seed, uint8_t count, int16_t avoidX, int16_t avoidY,
           int16_t avoidR) {
  uint32_t s = seed * 2654435761u + 12345;
  for (uint8_t i = 0; i < count; i++) {
    s = s * 1103515245u + 12345;
    const int16_t x =
        x0 + static_cast<int16_t>((s >> 8) % static_cast<uint32_t>(w));
    s = s * 1103515245u + 12345;
    const int16_t y =
        y0 + static_cast<int16_t>((s >> 8) % static_cast<uint32_t>(h));
    const int16_t dx = x - avoidX;
    const int16_t dy = y - avoidY;
    if (dx * dx + dy * dy < avoidR * avoidR)
      continue;
    if ((s >> 4) & 1)
      g.fillRect(x, y, 2, 2, kWhite);
    else
      g.drawPixel(x, y, kWhite);
  }
}

void drawSky(Adafruit_GFX &g, const View &v, const Theme &th) {
  const int16_t x0 = kSkyX;
  const int16_t w = kW - kSkyX;
  const int16_t hz = kSkyH - 12;
  gradV(g, x0, 0, w, kSkyH, th.sky, 3);
  constexpr float kAzMin = 25.0f;
  constexpr float kAzMax = 335.0f;
  constexpr float kElMax = 55.0f;
  constexpr int16_t kSunR = 7;
  auto mapX = [&](float az) {
    return static_cast<int16_t>(x0 + kSunR + 3 +
                                (az - kAzMin) / (kAzMax - kAzMin) *
                                    (w - 2 * kSunR - 6));
  };
  auto mapY = [&](float el) {
    return static_cast<int16_t>(hz - el / kElMax * (hz - kSunR - 4));
  };

  if (v.phase == Phase::Night) {
    const int16_t mx = kW - 22;
    const int16_t my = 18;
    stars(g, x0 + 2, 2, w - 4, hz - 10, static_cast<uint32_t>(v.agenda.today),
          14, mx, my, 16);
    g.fillCircle(mx, my, 10, kWhite);
    g.fillCircle(mx + 5, my - 4, 9, kBlack);
  } else {
    if (v.sunDay.rises) {
      for (time_t t = v.sunDay.sunrise; t <= v.sunDay.sunset; t += 900) {
        const SunPos p = sunPosition(t, v.lat, v.lon);
        if (p.elevation <= 0)
          continue;
        g.drawPixel(mapX(p.azimuth), mapY(p.elevation), th.pathDot);
      }
    }
    if (v.timeValid && v.sun.elevation > -kSunR) {
      int16_t sx = mapX(v.sun.azimuth);
      int16_t sy = mapY(v.sun.elevation);
      if (sx < x0 + kSunR + 2)
        sx = x0 + kSunR + 2;
      if (sx > kW - kSunR - 3)
        sx = kW - kSunR - 3;
      if (sy < kSunR + 2)
        sy = kSunR + 2;
      if (v.phase == Phase::Day) {
        for (uint8_t a = 0; a < 8; a++) {
          const float an = a * static_cast<float>(M_PI) / 4;
          g.drawLine(sx + cosf(an) * (kSunR + 3), sy + sinf(an) * (kSunR + 3),
                     sx + cosf(an) * (kSunR + 6), sy + sinf(an) * (kSunR + 6),
                     kBlack);
        }
        g.fillCircle(sx, sy, kSunR, kBlack);
        g.fillCircle(sx, sy, kSunR - 2, kWhite);
      } else {
        g.fillCircle(sx, sy, kSunR, kRed);
        g.drawCircle(sx, sy, kSunR, v.phase == Phase::Evening ? kBlack : kRed);
      }
    }
  }
  hills(g, x0, w, hz + 2, kSkyH, 0.6f, 6.0f, th.hillsBack);
  hills(g, x0, w, hz + 7, kSkyH, 2.1f, 4.0f, th.hillsFront);
}

void drawHeader(Adafruit_GFX &g, const View &v, const Theme &th) {
  int y, m, d;
  civilFromDays(v.agenda.today, y, m, d);
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", d);
  const int16_t nw = drawText(g, &FontHuge, kM - 2, 54, buf, th.fg);
  const int16_t tx = kM - 2 + nw + 8;
  const int16_t avail = kSkyX - 8 - tx;
  const char *wd = kWeekdays[weekdayOf(v.agenda.today)];
  const GFXfont *wf = measureText(&FontMB, wd) <= avail ? &FontMB : &FontSB;
  drawText(g, wf, tx, 27, wd, th.fg, Align::Left, avail);
  drawText(g, &FontS, tx, 45, kMonths[m - 1], th.fg, Align::Left, avail);

  int16_t x = kM;
  if (v.sunDay.rises) {
    g.fillTriangle(x, 65, x + 7, 65, x + 3, 59, th.fg);
    x += 11;
    formatHm(v.sunDay.sunrise, buf, sizeof(buf));
    x += drawText(g, &FontS, x, 66, buf, th.fg) + 9;
    g.fillTriangle(x, 59, x + 7, 59, x + 3, 65, th.fg);
    x += 11;
    formatHm(v.sunDay.sunset, buf, sizeof(buf));
    x += drawText(g, &FontS, x, 66, buf, th.fg) + 9;
  }
  if (v.timeValid) {
    const int el = static_cast<int>(lroundf(v.sun.elevation));
    snprintf(buf, sizeof(buf), "%s%d°", el < 0 ? "−" : "", el < 0 ? -el : el);
    drawText(g, &FontSB, x, 66, buf, th.fg);
  }
  drawSky(g, v, th);
  g.drawFastHLine(kM, 73, kW - 2 * kM, th.fg);
}

void drawCard(Adafruit_GFX &g, const View &v, const Theme &th) {
  const Agenda &a = v.agenda;
  const Snapshot &s = *v.snap;
  const int16_t x = kM + 10;
  const int16_t right = kW - kM - 10;
  const int16_t y = kCardY;
  const int16_t bandH = kCardY + kCardH - kBandY;
  char lab[160];
  char t1[16];
  char t2[16];
  g.fillRect(kM, y, kW - 2 * kM, kBandY - y, th.cardBg);
  g.fillRect(kM, kBandY, kW - 2 * kM, bandH, kBlack);
  if (th.cardBg == kBlack)
    g.drawFastHLine(x, kBandY, right - x, kWhite);
  if (a.card < 0) {
    drawText(g, &FontSB, x, y + 18, "Сегодня", th.cardFg);
    drawText(g, &FontXL, x, y + 46,
             a.timedCount ? "На сегодня всё" : "Свободный день", th.cardFg,
             Align::Left, right - x);
    if (a.tomorrowFirst >= 0) {
      const Event &e = s.events[a.tomorrowFirst];
      formatHm(e.start, t1, sizeof(t1));
      snprintf(lab, sizeof(lab), "завтра %s  %s", t1, str(s, e.title));
      drawText(g, &FontS, x, kBandY + 17, lab, kWhite, Align::Left, right - x);
    }
    return;
  }
  const Event &e = s.events[a.card];
  formatHm(e.start, t1, sizeof(t1));
  formatHm(e.end, t2, sizeof(t2));
  snprintf(lab, sizeof(lab), "%s · %s–%s", a.cardCurrent ? "Сейчас" : "Далее",
           t1, t2);
  const char *title = str(s, e.title);
  const int16_t titleW = right - x;
  if (measureText(&FontXL, title) <= titleW) {
    drawText(g, &FontSB, x, y + 18, lab, th.cardFg, Align::Left, titleW);
    drawText(g, &FontXL, x, y + 46, title, th.cardFg);
  } else if (wrapLines(&FontL, title, titleW, 2) == 1) {
    drawText(g, &FontSB, x, y + 18, lab, th.cardFg, Align::Left, titleW);
    drawText(g, &FontL, x, y + 44, title, th.cardFg);
  } else {
    drawText(g, &FontSB, x, y + 16, lab, th.cardFg, Align::Left, titleW);
    drawWrapped(g, &FontL, x, y + 35, 17, title, th.cardFg, titleW, 2);
  }

  char tail[32] = "";
  int16_t base = kBandY + 17;
  if (v.timeValid) {
    char d[16];
    if (a.cardCurrent) {
      const int32_t total = e.end - e.start;
      const int32_t done = static_cast<int32_t>(v.now - e.start);
      const int16_t bw = right - x;
      g.drawRect(x, kBandY + 4, bw, 5, kWhite);
      if (total > 0)
        g.fillRect(
            x, kBandY + 4,
            static_cast<int16_t>(static_cast<int32_t>(bw) * done / total), 5,
            kWhite);
      base = kBandY + 20;
      formatDuration(static_cast<int32_t>(e.end - v.now), d, sizeof(d));
      snprintf(tail, sizeof(tail), "ещё %s", d);
    } else {
      formatDuration(static_cast<int32_t>(e.start - v.now), d, sizeof(d));
      snprintf(tail, sizeof(tail), "через %s", d);
    }
  }
  const int16_t tw =
      tail[0] ? drawText(g, &FontSB, right, base, tail, kWhite, Align::Right)
              : 0;
  if (e.place)
    drawText(g, &FontS, x, base, str(s, e.place), kWhite, Align::Left,
             right - x - tw - 10);
}

void drawNextRow(Adafruit_GFX &g, const View &v, const Theme &th) {
  const Agenda &a = v.agenda;
  const Snapshot &s = *v.snap;
  const int16_t x = kM + 12;
  const int16_t y = kNextY;
  char head[48];
  char t1[16];
  int8_t idx = a.next;
  const char *label = "Потом";
  if (idx < 0) {
    idx = a.tomorrowFirst;
    label = "Завтра";
  }
  if (idx < 0) {
    fillTone(g, kM, y, 5, 34, themed({0, 30}, th));
    drawText(g, &FontS, x, y + 13, "Завтра", th.fg);
    drawText(g, &FontMB, x, y + 33, "свободно", th.fg);
    return;
  }
  const Event &e = s.events[idx];
  fillTone(g, kM, y, 5, 34, eventTone(v, e, th));
  formatHm(e.start, t1, sizeof(t1));
  snprintf(head, sizeof(head), "%s · %s", label, t1);
  drawText(g, &FontS, x, y + 13, head, th.fg);
  if (a.next >= 0 && v.timeValid) {
    char d[16];
    char c[32];
    formatDuration(static_cast<int32_t>(e.start - v.now), d, sizeof(d));
    snprintf(c, sizeof(c), "через %s", d);
    drawText(g, &FontL, kW - kM, y + 15, c, th.fg, Align::Right);
  }
  static const GFXfont *const kFonts[] = {&FontMB, &FontSB};
  const char *title = str(s, e.title);
  const int16_t w = kW - kM - x;
  drawText(g, fitFont(kFonts, 2, title, w), x, y + 33, title, th.fg,
           Align::Left, w);
}

void drawStrip(Adafruit_GFX &g, const View &v, const Theme &th) {
  const Agenda &a = v.agenda;
  const Snapshot &s = *v.snap;
  int startH = 8;
  for (uint8_t j = 0; j < a.timedCount; j++) {
    const Event &e = s.events[a.timed[j]];
    if (e.start > a.dayStart) {
      const int h = localMinute(e.start) / 60;
      if (h < startH)
        startH = h;
    }
  }
  const time_t from = a.dayStart + startH * 3600;
  const int32_t span = static_cast<int32_t>(a.dayEnd - from);
  const int16_t sx = kM;
  const int16_t sw = kW - 2 * kM;
  auto px = [&](time_t t) {
    if (t < from)
      t = from;
    if (t > a.dayEnd)
      t = a.dayEnd;
    return static_cast<int16_t>(sx +
                                static_cast<int32_t>(t - from) * sw / span);
  };
  fillTone(g, sx, kStripY, sw, kStripH, th.track);
  for (uint8_t j = 0; j < a.timedCount; j++) {
    const int8_t idx = a.timed[j];
    const Event &e = s.events[idx];
    const int16_t x1 = px(e.start);
    int16_t w = px(e.end) - x1 - 1;
    if (w < 2)
      w = 2;
    if (idx == a.card && a.cardCurrent) {
      g.fillRect(x1, kStripY, w, kStripH, kRed);
    } else {
      const Tone t = v.timeValid && e.end <= v.now
                         ? faded(eventTone(v, e, th), th)
                         : eventTone(v, e, th);
      fillTone(g, x1, kStripY, w, kStripH, t);
    }
  }
  if (v.timeValid && v.now >= from && v.now < a.dayEnd) {
    const int16_t nx = px(v.now);
    g.fillTriangle(nx - 5, kStripY - 7, nx + 5, kStripY - 7, nx, kStripY - 1,
                   th.fg);
  }
  char buf[12];
  for (int h = startH; h <= 24;
       h = h == startH ? (startH / 4 + 1) * 4 : h + 4) {
    if (h != startH && h != 24 && h - startH < 2)
      continue;
    snprintf(buf, sizeof(buf), "%d", h);
    int16_t x = px(from + (h - startH) * 3600);
    const int16_t hw = measureText(&FontS, buf) / 2;
    if (x - hw < sx)
      x = sx + hw;
    if (x + hw > sx + sw)
      x = sx + sw - hw;
    drawText(g, &FontS, x, kStripY + kStripH + 14, buf, th.fg, Align::Center);
  }
}

void drawList(Adafruit_GFX &g, const View &v, const Theme &th) {
  const Agenda &a = v.agenda;
  const Snapshot &s = *v.snap;
  int16_t y = kListY;
  if (a.allDayCount) {
    const char *tag = "весь день";
    const int16_t bw = measureText(&FontSB, tag) + 10;
    g.fillRect(kM, y, bw, 16, kRed);
    drawText(g, &FontSB, kM + bw / 2, y + 12, tag, kWhite, Align::Center);
    char more[8] = "";
    if (a.allDayCount > 1)
      snprintf(more, sizeof(more), "+%d", a.allDayCount - 1);
    const int16_t mw = more[0] ? drawText(g, &FontSB, kW - kM, y + 12, more,
                                          th.fg, Align::Right) +
                                     6
                               : 0;
    static const GFXfont *const kFonts[] = {&FontMB, &FontSB};
    const char *title = str(s, s.events[a.allDay[0]].title);
    const int16_t tw = kW - kM - mw - (kM + bw + 8);
    drawText(g, fitFont(kFonts, 2, title, tw), kM + bw + 8, y + 13, title,
             th.fg, Align::Left, tw);
    y += 20;
  }
  const uint8_t rows = static_cast<uint8_t>((kListEnd - y) / kListRowH);
  if (!a.restCount) {
    if (a.next >= 0 && rows)
      drawText(g, &FontS, kM + 12, y + 11, "дальше свободно", th.fg);
    return;
  }
  for (uint8_t i = 0; i < a.restCount && i < rows; i++) {
    const int16_t ry = y + i * kListRowH;
    if (i == rows - 1 && a.restCount > rows) {
      char more[24];
      snprintf(more, sizeof(more), "+%d %s", a.restCount - i,
               plural(a.restCount - i, "событие", "события", "событий"));
      drawText(g, &FontS, kM + 12, ry + 11, more, th.fg);
      break;
    }
    const Event &e = s.events[a.rest[i]];
    char t1[16];
    formatHm(e.start, t1, sizeof(t1));
    fillTone(g, kM, ry + 1, 5, 12, eventTone(v, e, th));
    drawText(g, &FontSB, kM + 12, ry + 11, t1, th.fg);
    drawText(g, &FontS, kM + 54, ry + 11, str(s, e.title), th.fg, Align::Left,
             kW - kM - (kM + 54));
  }
}

void drawTodos(Adafruit_GFX &g, const View &v, const Theme &th) {
  const Agenda &a = v.agenda;
  const Snapshot &s = *v.snap;
  g.fillRect(kM, kTodoY, kW - 2 * kM, 2, th.fg);
  drawText(g, &FontMB, kM, kTodoY + 18, "Задачи", th.fg);
  char buf[48];
  if (a.overdue && a.dueToday)
    snprintf(buf, sizeof(buf), "%d просрочено · %d сегодня", a.overdue,
             a.dueToday);
  else if (a.overdue)
    snprintf(buf, sizeof(buf), "%d просрочено", a.overdue);
  else if (a.dueToday)
    snprintf(buf, sizeof(buf), "%d на сегодня", a.dueToday);
  else
    snprintf(buf, sizeof(buf), "всё сделано");
  drawText(g, &FontS, kW - kM, kTodoY + 18, buf, th.fg, Align::Right);

  const uint8_t rows =
      static_cast<uint8_t>((kFooterY - 2 - kTodoRowsY) / kTodoRowH);
  for (uint8_t i = 0; i < a.todoCount && i < rows; i++) {
    const int16_t y = kTodoRowsY + i * kTodoRowH;
    if (i == rows - 1 && a.todoCount > rows) {
      snprintf(buf, sizeof(buf), "+%d %s", a.todoCount - i,
               plural(a.todoCount - i, "задача", "задачи", "задач"));
      drawText(g, &FontS, kM + 24, y + 11, buf, th.fg);
      break;
    }
    const Todo &t = s.todos[a.todos[i]];
    const int late = static_cast<int>(a.today - t.dueDay);
    int16_t rx = kW - kM;
    if (late > 0) {
      const uint8_t sev =
          static_cast<uint8_t>(late > kOverdueDays ? kOverdueDays : late);
      fillTone(
          g, kM, y + 1, 3, 12,
          {static_cast<uint8_t>(30 + sev * 2), static_cast<uint8_t>(sev * 2)});
      snprintf(buf, sizeof(buf), "−%d дн", late);
      const int16_t bw = measureText(&FontSB, buf) + 8;
      rx -= bw;
      g.fillRect(rx, y, bw, 13, kRed);
      drawText(g, &FontSB, rx + bw / 2, y + 11, buf, kWhite, Align::Center);
    } else {
      buf[0] = '\0';
      if (t.dueMin >= 0)
        formatMinutes(t.dueMin, buf, sizeof(buf));
      if (t.checksTotal) {
        const size_t n = strlen(buf);
        snprintf(buf + n, sizeof(buf) - n, "%s%d/%d", n ? " · " : "",
                 t.checksDone, t.checksTotal);
      }
      if (buf[0])
        rx -= drawText(g, &FontS, rx, y + 11, buf, th.fg, Align::Right);
    }
    g.drawRect(kM + 9, y + 2, 10, 10, late > 0 ? kRed : th.fg);
    if (late > 0)
      g.drawRect(kM + 10, y + 3, 8, 8, kRed);
    drawText(g, late > 0 ? &FontSB : &FontS, kM + 25, y + 11, str(s, t.title),
             th.fg, Align::Left, rx - 8 - (kM + 25));
  }
  if (!a.todoCount)
    drawText(g, &FontS, kM + 25, kTodoRowsY + 11, "ничего не горит", th.fg);
}

void drawFooter(Adafruit_GFX &g, const View &v, const char *left, bool dark) {
  if (dark)
    g.drawFastHLine(0, kFooterY - 1, kW, kWhite);
  g.fillRect(0, kFooterY, kW, kH - kFooterY, kBlack);
  char buf[32];
  char hm[16];
  if (v.timeValid) {
    formatHm(v.now, hm, sizeof(hm));
    drawText(g, &FontSB, kW - kM, kH - 4, hm, kWhite, Align::Right);
  }
  int16_t rx = kClockX - 4;
  if (!v.online) {
    formatHm(v.snap->fetchedAt, hm, sizeof(hm));
    snprintf(buf, sizeof(buf), "офлайн · %s", hm);
    const int16_t bw = measureText(&FontSB, buf) + 10;
    rx -= bw;
    g.fillRect(rx, kFooterY + 2, bw, kH - kFooterY - 4, kRed);
    drawText(g, &FontSB, rx + bw / 2, kH - 4, buf, kWhite, Align::Center);
  }
  drawText(g, &FontS, kM, kH - 4, left, kWhite, Align::Left, rx - 10 - kM);
}

void tomorrowLine(const View &v, char *out, size_t cap) {
  const Agenda &a = v.agenda;
  if (a.tomorrowFirst < 0) {
    snprintf(out, cap,
             a.tomorrowCount ? "Завтра весь день: %d" : "Завтра свободно",
             a.tomorrowCount);
    return;
  }
  const Event &e = v.snap->events[a.tomorrowFirst];
  char t1[16];
  formatHm(e.start, t1, sizeof(t1));
  if (a.tomorrowCount > 1)
    snprintf(out, cap, "Завтра  %s %s  +%d", t1, str(*v.snap, e.title),
             a.tomorrowCount - 1);
  else
    snprintf(out, cap, "Завтра  %s %s", t1, str(*v.snap, e.title));
}

} // namespace

const Rect kLiveRegions[kLiveRegionCount] = {
    {kM, kBandY, kW - 2 * kM, kCardY + kCardH - kBandY},
    {136, kCardY + kCardH, kW - 136, 24},
    {0, kStripY - 8, kW, 8},
    {kClockX, kFooterY, kW - kClockX, kH - kFooterY},
};

View makeView(const Snapshot &s, time_t now, bool timeValid, bool online,
              double lat, double lon) {
  View v = {};
  v.snap = &s;
  v.now = now;
  v.timeValid = timeValid;
  v.online = online;
  v.lat = lat;
  v.lon = lon;
  v.agenda = buildAgenda(s, now, timeValid);
  v.sun = sunPosition(now, lat, lon);
  v.sunDay = sunDay(v.agenda.dayStart, lat, lon);
  v.phase = timeValid ? sunPhase(now, v.sunDay, v.sun) : Phase::Day;
  v.sleep = timeValid ? sleepState(s, now) : SleepState{false, 0, -1};
  return v;
}

void renderDay(Adafruit_GFX &g, const View &v) {
  const Theme th = themeFor(v.phase);
  g.fillScreen(th.bg);
  drawHeader(g, v, th);
  drawCard(g, v, th);
  drawNextRow(g, v, th);
  drawStrip(g, v, th);
  drawList(g, v, th);
  drawTodos(g, v, th);
  char left[160];
  if (v.phase == Phase::Night && v.sleep.tonightStart >= 0) {
    char hm[16];
    formatMinutes(v.sleep.tonightStart, hm, sizeof(hm));
    snprintf(left, sizeof(left), "сон с %s", hm);
  } else {
    tomorrowLine(v, left, sizeof(left));
  }
  drawFooter(g, v, left, th.dark);
}

void renderSleep(Adafruit_GFX &g, const View &v) {
  const Snapshot &s = *v.snap;
  g.fillScreen(kWhite);
  const int16_t skyH = 276;
  const Tone sky[3] = {{0, 64}, {0, 62}, {2, 44}};
  gradV(g, 0, 0, kW, skyH, sky, 3);
  stars(g, 4, 4, kW - 8, 86, static_cast<uint32_t>(v.agenda.today), 18, 228, 60,
        40);
  stars(g, 4, 172, kW - 8, 36, static_cast<uint32_t>(v.agenda.today) + 7, 7, 0,
        0, 0);
  g.fillCircle(228, 60, 26, kWhite);
  g.fillCircle(241, 51, 24, kBlack);
  hills(g, 0, kW, 222, skyH, 0.4f, 18.0f, {0, 54});
  hills(g, 0, kW, 250, skyH, 1.9f, 10.0f, {0, 38});
  drawText(g, &FontXL, 20, 124, "Спокойной ночи", kWhite);
  char buf[160];
  char hm[16];
  formatHm(v.sleep.wakeAt, hm, sizeof(hm));
  snprintf(buf, sizeof(buf), "экран проснётся в %s", hm);
  drawText(g, &FontS, 20, 145, buf, kWhite);
  g.fillRect(16, skyH + 6, kW - 32, 2, kRed);

  const int32_t target = localDay(v.sleep.wakeAt);
  const time_t from = localMidnight(target);
  const time_t to = localMidnight(target + 1);
  int y, m, d;
  civilFromDays(target, y, m, d);
  snprintf(buf, sizeof(buf), "%s · %s, %d %s",
           target == v.agenda.today ? "Сегодня" : "Завтра",
           kWeekdays[weekdayOf(target)], d, kMonthsGen[m - 1]);
  drawText(g, &FontMB, 16, 302, buf, kBlack, Align::Left, kW - 32);

  int8_t first = -1;
  uint8_t count = 0;
  for (uint8_t i = 0; i < s.eventCount; i++) {
    const Event &e = s.events[i];
    if (e.start >= to || e.end <= from)
      continue;
    count++;
    if (first < 0 && !(e.flags & kEventAllDay))
      first = i;
  }
  uint8_t todos = 0;
  for (uint8_t i = 0; i < s.todoCount; i++) {
    if (s.todos[i].dueDay <= target &&
        s.todos[i].dueDay >= target - kOverdueDays)
      todos++;
  }
  if (first >= 0) {
    const Event &e = s.events[first];
    formatHm(e.start, hm, sizeof(hm));
    const int16_t tw = drawText(g, &FontBig, 14, 344, hm, kBlack);
    const int16_t tx = 14 + tw + 12;
    char end[16];
    formatHm(e.end, end, sizeof(end));
    snprintf(buf, sizeof(buf), "до %s", end);
    drawText(g, &FontMB, tx, 326, buf, kBlack);
    if (e.place)
      drawText(g, &FontS, tx, 343, str(s, e.place), kBlack, Align::Left,
               kW - 16 - tx);
    const char *title = str(s, e.title);
    if (measureText(&FontL, title) <= kW - 32)
      drawText(g, &FontL, 16, 370, title, kBlack);
    else
      drawWrapped(g, &FontMB, 16, 364, 15, title, kBlack, kW - 32, 2);
    count--;
  } else {
    drawText(g, &FontXL, 16, 350, count ? "Только весь день" : "Свободный день",
             kBlack);
  }
  formatHm(v.sleep.wakeAt, hm, sizeof(hm));
  int n = snprintf(buf, sizeof(buf), "сон до %s", hm);
  if (count)
    n += snprintf(buf + n, sizeof(buf) - n, " · ещё %d %s", count,
                  plural(count, "событие", "события", "событий"));
  if (todos)
    snprintf(buf + n, sizeof(buf) - n, " · %d %s", todos,
             plural(todos, "задача", "задачи", "задач"));
  drawFooter(g, v, buf, false);
}

void renderMessage(Adafruit_GFX &g, const char *title, const char *body) {
  g.fillScreen(kWhite);
  g.fillRect(0, 0, kW, 6, kRed);
  drawText(g, &FontXL, kW / 2, 180, title, kBlack, Align::Center, kW - 2 * kM);
  drawText(g, &FontS, kW / 2, 206, body, kBlack, Align::Center, kW - 2 * kM);
}
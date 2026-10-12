#include "app.h"

#include <ESP8266WiFi.h>
#include <GxEPD2_3C.h>
#include <new>
#include <umm_malloc/umm_heap_select.h>

#include "agenda.h"
#include "api.h"
#include "config.h"
#include "picture.h"
#include "render.h"
#include "store.h"
#include "timeutil.h"

namespace {

GxEPD2_3C<GxEPD2_420c_GDEY042Z98, GxEPD2_420c_GDEY042Z98::HEIGHT / 8>
    display(GxEPD2_420c_GDEY042Z98(cfg::kPinCs, cfg::kPinDc, cfg::kPinRst,
                                   cfg::kPinBusy));
Snapshot gSnap;

template <typename F> void paint(F draw) {
  display.setRotation(DISPLAY_ROTATION);
  display.setFullWindow();
  display.firstPage();
  do {
    draw(display);
  } while (display.nextPage());
  display.hibernate();
}

class LiveCanvas final : public Adafruit_GFX {
public:
  LiveCanvas() : Adafruit_GFX(300, 400) {}

  void clear() { memset(bits_, 0xFF, sizeof(bits_)); }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    for (uint8_t i = 0; i < kLiveRegionCount; i++) {
      const Rect &r = kLiveRegions[i];
      if (x < r.x || x >= r.x + r.w || y < r.y || y >= r.y + r.h)
        continue;
      const uint16_t nx = static_cast<uint16_t>(r.y + r.h - 1 - y);
      const uint16_t ny = static_cast<uint16_t>(x - r.x);
      const uint32_t bit = static_cast<uint32_t>(ny) * r.h + nx;
      uint8_t &b = bits_[offset(i) + bit / 8];
      const uint8_t mask = 0x80 >> (bit % 8);
      if (color == kWhite)
        b |= mask;
      else
        b &= ~mask;
      return;
    }
  }

  const uint8_t *region(uint8_t i) const { return bits_ + offset(i); }
  size_t regionSize(uint8_t i) const {
    return static_cast<size_t>(kLiveRegions[i].w) * kLiveRegions[i].h / 8;
  }

private:
  static size_t offset(uint8_t i) {
    size_t o = 0;
    for (uint8_t j = 0; j < i; j++)
      o += static_cast<size_t>(kLiveRegions[j].w) * kLiveRegions[j].h / 8;
    return o;
  }

  uint8_t bits_[kLiveRegionBytes];
};

// Latched in the ISR so presses during a refresh or a sync are not lost.
volatile bool gButtonHit = false;

IRAM_ATTR void onButton() { gButtonHit = true; }

uint32_t mix(uint32_t h, uint32_t v) {
  h ^= v;
  return h * 16777619u;
}

} // namespace

void App::setup() {
  Serial.begin(115200);
  Serial.println();
  pinMode(cfg::kPinButton, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(cfg::kPinButton), onButton, FALLING);
  display.init(115200, true, 50, false);
  // After display.init(): it restarts Serial and would eat this line.
  Serial.printf("boot: %s\n", ESP.getResetReason().c_str());
  storeBegin();
  if (storeLoad(gSnap)) {
    haveData_ = true;
    dataHash_ = snapshotHash(gSnap);
    weather_ = gSnap.weather;
    Serial.printf("cache: %u events from %ld\n", gSnap.eventCount,
                  static_cast<long>(gSnap.fetchedAt));
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiAttemptMs_ = millis();
  configTime(TZ_POSIX, "pool.ntp.org", "time.cloudflare.com");

  const uint32_t start = millis();
  while (millis() - start < cfg::kBootWaitMs &&
         !(WiFi.status() == WL_CONNECTED && timeReady()))
    delay(250);
  if (timeReady()) {
    const time_t now = time(nullptr);
    sync(now);
    refresh(now);
  } else if (haveData_) {
    online_ = false;
    refresh(gSnap.fetchedAt);
  } else {
    paint([](Adafruit_GFX &g) {
      renderMessage(g, "Нет связи", "Проверь Wi-Fi в .env и перезагрузи");
    });
  }
}

void App::loop() {
  const uint32_t start = millis();
  while (millis() - start < cfg::kLoopMs) {
    if (buttonPressed()) {
      togglePicture();
      return;
    }
    delay(cfg::kButtonPollMs);
  }
  if (!timeReady()) {
    wifiReady();
    return;
  }
  const time_t now = time(nullptr);
  if (now - lastPoll_ >= static_cast<time_t>(cfg::kPollSec))
    sync(now);
  if (picture_)
    return;
  refresh(now);
}

bool App::buttonPressed() {
  if (!gButtonHit)
    return false;
  // Wait for a clean release so its bounce is not taken for a second press.
  const uint32_t start = millis();
  uint32_t high = millis();
  while (millis() - start < 2000 && millis() - high < cfg::kButtonDebounceMs) {
    if (digitalRead(cfg::kPinButton) == LOW)
      high = millis();
    delay(5);
  }
  gButtonHit = false;
  if (millis() - buttonMs_ < cfg::kButtonGapMs)
    return false;
  buttonMs_ = millis();
  return true;
}

void App::togglePicture() {
  picture_ = !picture_;
  Serial.printf("button: picture %d\n", picture_);
  if (picture_) {
    liveReady_ = false;
    // Native 400x300 orientation, arrays read straight from flash.
    display.drawImage(imageBlack, imageRed, 0, 0, 400, 300, false, false, true);
    display.hibernate();
    return;
  }
  lastKey_ = 0;
  lastFull_ = 0;
  refresh(timeReady() ? time(nullptr) : gSnap.fetchedAt);
}

bool App::wifiReady() {
  if (WiFi.status() == WL_CONNECTED)
    return true;
  if (millis() - wifiAttemptMs_ > cfg::kWifiRetryMs) {
    wifiAttemptMs_ = millis();
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  return false;
}

bool App::timeReady() const { return time(nullptr) > 1700000000; }

void App::sync(time_t now) {
  lastPoll_ = now;
  if (!wifiReady()) {
    online_ = false;
    return;
  }
  // A new day re-asks without the ETag so the window always moves forward.
  const bool conditional = haveData_ && gSnap.firstDay == localDay(now);
  switch (apiFetchDisplay(gSnap, conditional)) {
  case FetchResult::Updated:
    online_ = true;
    haveData_ = true;
    dataHash_ = snapshotHash(gSnap);
    weather_ = gSnap.weather;
    if (!storeSave(gSnap))
      Serial.println("cache: save failed");
    break;
  case FetchResult::NotModified:
    online_ = true;
    gSnap.fetchedAt = static_cast<int32_t>(now);
    break;
  case FetchResult::Failed:
    online_ = false;
    break;
  }
}

uint32_t App::viewKey(time_t now, bool timeValid) const {
  uint32_t h = mix(2166136261u, dataHash_);
  h = mix(h, online_);
  h = mix(h, timeValid);
  if (!timeValid)
    return h;
  const View v =
      makeView(gSnap, now, true, online_, GEO_LAT, GEO_LON, weather_);
  h = mix(h, static_cast<uint32_t>(v.agenda.today));
  h = mix(h, v.sleep.sleeping);
  if (v.sleep.sleeping)
    return h;
  h = mix(h, static_cast<uint32_t>(v.phase));
  h = mix(h, skyKind(v));
  h = mix(h, static_cast<uint32_t>(v.agenda.card + 1));
  h = mix(h, v.agenda.cardCurrent);
  return mix(h, static_cast<uint32_t>(v.agenda.next + 1));
}

void App::refresh(time_t now) {
  if (!haveData_)
    return;
  const bool timeValid = timeReady();
  const uint32_t key = viewKey(now, timeValid);
  // A day screen that has not changed for kStuckSec means partials stopped
  // working; fall back to a full refresh instead of freezing silently.
  const bool stuck = liveReady_ && timeValid && drawnAt_ &&
                     now - drawnAt_ >= static_cast<time_t>(cfg::kStuckSec);
  const bool stale = lastFull_ && timeValid &&
                     (now - lastFull_ >= static_cast<time_t>(cfg::kCleanSec) ||
                      partials_ >= cfg::kMaxPartials);
  if (key != lastKey_ || stale || stuck) {
    if (lastFull_ && timeValid &&
        now - lastFull_ < static_cast<time_t>(cfg::kMinRenderGapSec))
      return;
    if (stuck)
      Serial.printf("render: stuck for %ld s\n",
                    static_cast<long>(now - drawnAt_));
    renderFull(now, timeValid);
    lastKey_ = key;
    return;
  }
  if (liveReady_ && timeValid &&
      now - lastLive_ >= static_cast<time_t>(cfg::kPartialSec))
    renderLive(now);
}

void App::renderFull(time_t now, bool timeValid) {
  const View v =
      makeView(gSnap, now, timeValid, online_, GEO_LAT, GEO_LON, weather_);
  Serial.printf("render: full phase %u sleeping %d online %d heap %u\n",
                static_cast<unsigned>(v.phase), v.sleep.sleeping, online_,
                ESP.getFreeHeap());
  if (v.sleep.sleeping)
    paint([&v](Adafruit_GFX &g) { renderSleep(g, v); });
  else
    paint([&v](Adafruit_GFX &g) { renderDay(g, v); });
  liveReady_ = timeValid && !v.sleep.sleeping;
  shownAt_ = now;
  shownWeather_ = weather_;
  lastFull_ = now;
  lastLive_ = now;
  drawnAt_ = now;
  partials_ = 0;
}

LiveCanvas *newCanvas() {
  LiveCanvas *c = new (std::nothrow) LiveCanvas();
  if (c)
    return c;
  HeapSelectIram iram;
  return new (std::nothrow) LiveCanvas();
}

void App::renderLive(time_t now) {
  LiveCanvas *before = newCanvas();
  LiveCanvas *after = newCanvas();
  if (!before || !after) {
    delete before;
    delete after;
    Serial.printf("render: live no memory heap %u block %u\n",
                  ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
    lastLive_ = now;
    return;
  }
  before->clear();
  after->clear();
  renderDay(*before, makeView(gSnap, shownAt_, true, online_, GEO_LAT, GEO_LON,
                              shownWeather_));
  renderDay(*after,
            makeView(gSnap, now, true, online_, GEO_LAT, GEO_LON, weather_));
  uint8_t updated = 0;
  for (uint8_t i = 0; i < kLiveRegionCount; i++) {
    const size_t n = after->regionSize(i);
    if (!memcmp(before->region(i), after->region(i), n))
      continue;
    const Rect &r = kLiveRegions[i];
    const int16_t nx =
        static_cast<int16_t>(GxEPD2_420c_GDEY042Z98::WIDTH - r.y - r.h);
    display.epd2.writeImageToPrevious(before->region(i), nx, r.x, r.h, r.w);
    display.epd2.writeImageToCurrent(after->region(i), nx, r.x, r.h, r.w);
    display.epd2.refresh_bw(nx, r.x, r.h, r.w);
    updated++;
  }
  delete before;
  delete after;
  if (updated) {
    display.epd2.hibernate();
    partials_++;
    drawnAt_ = now;
  }
  shownAt_ = now;
  shownWeather_ = weather_;
  lastLive_ = now;
}

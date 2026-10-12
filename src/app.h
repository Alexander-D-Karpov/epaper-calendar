#pragma once
#include <stdint.h>
#include <time.h>

#include "weather.h"

class App {
public:
  void setup();
  void loop();

private:
  bool wifiReady();
  bool timeReady() const;
  void sync(time_t now);
  void refresh(time_t now);
  void renderFull(time_t now, bool timeValid);
  void renderLive(time_t now);
  uint32_t viewKey(time_t now, bool timeValid) const;
  bool buttonPressed();
  void togglePicture();

  bool haveData_ = false;
  bool online_ = false;
  bool liveReady_ = false;
  bool picture_ = false;
  uint32_t buttonMs_ = 0;
  uint32_t dataHash_ = 0;
  uint32_t lastKey_ = 0;
  uint32_t wifiAttemptMs_ = 0;
  uint16_t partials_ = 0;
  time_t lastPoll_ = 0;
  time_t lastFull_ = 0;
  time_t lastLive_ = 0;
  time_t shownAt_ = 0;
  time_t drawnAt_ = 0;
  Weather weather_ = {};
  Weather shownWeather_ = {};
};

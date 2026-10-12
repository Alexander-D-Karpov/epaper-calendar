#pragma once
#include <stdint.h>

#ifndef WIFI_SSID
#error "WIFI_SSID is missing, fill .env"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
#ifndef CAL_TOKEN
#error "CAL_TOKEN is missing, fill .env"
#endif
#ifndef CAL_HOST
#define CAL_HOST "calendar.akarpov.ru"
#endif
#ifndef CAL_PORT
#define CAL_PORT 443
#endif
#ifndef CAL_INSECURE
#define CAL_INSECURE 0
#endif
#ifndef TZ_POSIX
#define TZ_POSIX "MSK-3"
#endif
#ifndef GEO_LAT
#define GEO_LAT 59.9386
#endif
#ifndef GEO_LON
#define GEO_LON 30.3141
#endif
#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION 1
#endif

namespace cfg {
constexpr uint8_t kPinCs = 12;
constexpr uint8_t kPinDc = 4;
constexpr uint8_t kPinRst = 5;
constexpr uint8_t kPinBusy = 16;
constexpr uint8_t kPinButton = 0;
constexpr uint32_t kButtonPollMs = 20;
constexpr uint32_t kButtonDebounceMs = 50;
constexpr uint32_t kButtonGapMs = 300;
constexpr uint32_t kLoopMs = 15000;
constexpr uint32_t kPollSec = 180;
constexpr uint32_t kRefetchSec = 6 * 3600;
constexpr uint32_t kMinRenderGapSec = 180;
constexpr uint32_t kPartialSec = 60;
constexpr uint32_t kCleanSec = 4 * 3600;
constexpr uint16_t kMaxPartials = 240;
constexpr uint32_t kWifiRetryMs = 30000;
constexpr uint32_t kBootWaitMs = 45000;
constexpr int kEventDays = 7;
constexpr int kOverdueDays = 14;
} // namespace cfg
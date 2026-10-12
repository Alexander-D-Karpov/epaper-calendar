#pragma once
#include <stdint.h>

struct Weather {
  bool valid;
  bool isDay;
  uint8_t code;
  int16_t tempX10;
  int32_t fetchedAt;
};

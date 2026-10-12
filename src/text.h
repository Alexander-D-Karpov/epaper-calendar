#pragma once
#include <Adafruit_GFX.h>
#include <stddef.h>

enum class Align : uint8_t { Left, Center, Right };

size_t encodeText(const char *utf8, char *out, size_t cap);
size_t copyUtf8(char *dst, size_t cap, const char *src);
int16_t textWidth(const GFXfont *font, const char *encoded);
int16_t measureText(const GFXfont *font, const char *utf8);
const GFXfont *fitFont(const GFXfont *const *fonts, uint8_t count,
                       const char *utf8, int16_t maxWidth);
uint8_t wrapLines(const GFXfont *font, const char *utf8, int16_t maxWidth,
                  uint8_t maxLines);
uint8_t drawWrapped(Adafruit_GFX &g, const GFXfont *font, int16_t x, int16_t y,
                    int16_t lineHeight, const char *utf8, uint16_t color,
                    int16_t maxWidth, uint8_t maxLines);
int16_t drawText(Adafruit_GFX &g, const GFXfont *font, int16_t x, int16_t y,
                 const char *utf8, uint16_t color, Align align = Align::Left,
                 int16_t maxWidth = 0);

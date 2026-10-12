#include "text.h"

#include <string.h>

#include "fonts_gen.h"

namespace {

uint8_t codeFor(uint16_t cp) {
  if (cp >= 0x20 && cp < 0x7F)
    return static_cast<uint8_t>(cp);
  int lo = 0;
  int hi = kGlyphCount - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const uint16_t u = pgm_read_word(&kGlyphUnicode[mid]);
    if (u == cp)
      return pgm_read_byte(&kGlyphCode[mid]);
    if (u < cp)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return '?';
}

int16_t advance(const GFXfont *font, uint8_t c) {
  const uint8_t first = pgm_read_byte(&font->first);
  const uint8_t last = pgm_read_byte(&font->last);
  if (c < first || c > last)
    return 0;
  const GFXglyph *glyphs =
      reinterpret_cast<const GFXglyph *>(pgm_read_ptr(&font->glyph));
  return pgm_read_byte(&glyphs[c - first].xAdvance);
}

constexpr size_t kTextCap = 128;

int16_t spanWidth(const GFXfont *font, const char *buf, size_t from,
                  size_t to) {
  int16_t w = 0;
  for (size_t i = from; i < to; i++)
    w += advance(font, static_cast<uint8_t>(buf[i]));
  return w;
}

size_t lineEnd(const GFXfont *font, const char *buf, size_t pos, size_t n,
               int16_t maxWidth, bool skipSpaces) {
  int16_t w = 0;
  size_t end = pos;
  while (end < n &&
         w + advance(font, static_cast<uint8_t>(buf[end])) <= maxWidth) {
    w += advance(font, static_cast<uint8_t>(buf[end]));
    end++;
  }
  if (end < n) {
    size_t space = end;
    while (space > pos && buf[space] != ' ')
      space--;
    if (space > pos)
      end = space;
  }
  if (end == pos && pos < n)
    end = pos + 1;
  if (skipSpaces) {
    while (end < n && buf[end] == ' ')
      end++;
  }
  return end;
}

int16_t fitEncoded(const GFXfont *font, char *buf, size_t n, int16_t maxWidth) {
  int16_t w = spanWidth(font, buf, 0, n);
  if (w <= maxWidth)
    return w;
  const uint8_t ell = codeFor(0x2026);
  const int16_t ellW = advance(font, ell);
  size_t cut = n;
  while (cut > 0 && spanWidth(font, buf, 0, cut) + ellW > maxWidth)
    cut--;
  size_t space = cut;
  while (space > 0 && buf[space] != ' ')
    space--;
  if (space > 0 && spanWidth(font, buf, 0, space) * 10 >= maxWidth * 6)
    cut = space;
  while (cut > 0 &&
         (buf[cut - 1] == ' ' || buf[cut - 1] == ',' || buf[cut - 1] == '-'))
    cut--;
  buf[cut] = static_cast<char>(ell);
  buf[cut + 1] = '\0';
  return spanWidth(font, buf, 0, cut) + ellW;
}

void printEncoded(Adafruit_GFX &g, const GFXfont *font, int16_t x, int16_t y,
                  const char *buf, uint16_t color) {
  g.setFont(font);
  g.setTextWrap(false);
  g.setTextColor(color);
  g.setCursor(x, y);
  g.print(buf);
}

} // namespace

size_t encodeText(const char *utf8, char *out, size_t cap) {
  size_t n = 0;
  const uint8_t *p = reinterpret_cast<const uint8_t *>(utf8);
  while (*p && n + 1 < cap) {
    uint16_t cp;
    if (*p < 0x80) {
      cp = *p++;
    } else if ((*p & 0xE0) == 0xC0 && p[1]) {
      cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
      p += 2;
    } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
      cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      p += 3;
    } else {
      p++;
      while ((*p & 0xC0) == 0x80)
        p++;
      cp = '?';
    }
    if (cp == '\n' || cp == '\r' || cp == '\t')
      cp = ' ';
    out[n++] = static_cast<char>(codeFor(cp));
  }
  out[n] = '\0';
  return n;
}

size_t copyUtf8(char *dst, size_t cap, const char *src) {
  if (!cap)
    return 0;
  if (!src)
    src = "";
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && (static_cast<uint8_t>(src[n]) & 0xC0) == 0x80)
      n--;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
  return n;
}

int16_t textWidth(const GFXfont *font, const char *encoded) {
  int16_t w = 0;
  for (const char *p = encoded; *p; p++)
    w += advance(font, static_cast<uint8_t>(*p));
  return w;
}

int16_t measureText(const GFXfont *font, const char *utf8) {
  char buf[kTextCap];
  encodeText(utf8, buf, sizeof(buf));
  return textWidth(font, buf);
}

const GFXfont *fitFont(const GFXfont *const *fonts, uint8_t count,
                       const char *utf8, int16_t maxWidth) {
  for (uint8_t i = 0; i < count; i++) {
    if (measureText(fonts[i], utf8) <= maxWidth)
      return fonts[i];
  }
  return fonts[count - 1];
}

uint8_t wrapLines(const GFXfont *font, const char *utf8, int16_t maxWidth,
                  uint8_t maxLines) {
  char buf[kTextCap];
  const size_t n = encodeText(utf8, buf, sizeof(buf));
  size_t pos = 0;
  uint8_t lines = 0;
  while (pos < n && lines < maxLines) {
    pos = lineEnd(font, buf, pos, n, maxWidth, true);
    lines++;
  }
  return lines ? lines : 1;
}

uint8_t drawWrapped(Adafruit_GFX &g, const GFXfont *font, int16_t x, int16_t y,
                    int16_t lineHeight, const char *utf8, uint16_t color,
                    int16_t maxWidth, uint8_t maxLines) {
  char buf[kTextCap];
  const size_t n = encodeText(utf8, buf, sizeof(buf));
  size_t pos = 0;
  uint8_t line = 0;
  while (pos < n && line < maxLines) {
    char out[kTextCap];
    size_t len;
    if (line == maxLines - 1) {
      len = n - pos;
      memcpy(out, buf + pos, len);
      out[len] = '\0';
      fitEncoded(font, out, len, maxWidth);
      pos = n;
    } else {
      const size_t end = lineEnd(font, buf, pos, n, maxWidth, true);
      len = end - pos;
      while (len > 0 && buf[pos + len - 1] == ' ')
        len--;
      memcpy(out, buf + pos, len);
      out[len] = '\0';
      pos = end;
    }
    printEncoded(g, font, x, y + line * lineHeight, out, color);
    line++;
  }
  return line;
}

int16_t drawText(Adafruit_GFX &g, const GFXfont *font, int16_t x, int16_t y,
                 const char *utf8, uint16_t color, Align align,
                 int16_t maxWidth) {
  char buf[kTextCap];
  size_t n = encodeText(utf8, buf, sizeof(buf));
  const int16_t w =
      maxWidth > 0 ? fitEncoded(font, buf, n, maxWidth) : textWidth(font, buf);
  int16_t cx = x;
  if (align == Align::Center)
    cx = x - w / 2;
  if (align == Align::Right)
    cx = x - w;
  printEncoded(g, font, cx, y, buf, color);
  return w;
}

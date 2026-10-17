#pragma once

#include <stdint.h>
#include <string.h>

using ColorVal = uint16_t;

class UIColor {
public:
  // color definitions (by element _type_)
  static ColorVal window_bkg, title_bkg, title_txt, primary_txt, secondary_txt, warning_txt, popup_bkg, popup_txt, corp_blue;
};

class DisplayDriver {
protected:
  int _w, _h;
  DisplayDriver(int w, int h) { _w = w; _h = h; }
#ifdef CYRILLIC_SUPPORT
  uint8_t _font_size = 1;     // current text size; used for baseline offset and word-wrap metrics
  int _cursor_y_raw = 0;      // logical y before GFXfont baseline shift; tracks line position in printWordWrap
#endif
public:
  //enum Color { DARK=0, LIGHT, RED, GREEN, BLUE, YELLOW, ORANGE }; // on b/w screen, colors will be !=0 synonym of light

  int width() const { return _w; }
  int height() const { return _h; }

  virtual void setRotation(uint8_t r) {}
  virtual void forceFullRefresh() {}       // force full eInk refresh on next frame
  virtual void fullRefreshAndHibernate() {} // full refresh + hibernate for clean shutdown
  // Аппаратный контраст панели (0..255). Есть только у OLED; у eInk и TFT
  // это пустышка, чтобы UI мог звать метод не задумываясь.
  virtual void setContrast(uint8_t contrast) { (void)contrast; }
  // Line pitch in pixels for the given text size: cell height + inter-line gap.
  virtual int getLineH(int sz) const { return 8 * sz + 3; }
  virtual bool isEink() const { return false; }
  virtual bool isColorTFT() const { return false; }
  virtual void setFullRefreshSuppressed(bool s) {}
#if defined(WITH_ASYNC_EINK)
  // Periodic service hook for drivers that complete hardware work in stages.
  virtual void service() {}
  virtual bool isRefreshBusy() const { return false; }
  // Block until that work finishes, or the deadline passes. Callers that
  // genuinely must wait — shutdown, the boot splash, anything about to
  // overwrite the framebuffer — go through this instead of open-coding the
  // loop: an unbounded wait on a panel that never releases BUSY hangs the
  // firmware outright, and there is no watchdog behind the superloop.
  // Default is a no-op: a driver with no staged work is never busy. The bounded
  // loop lives in the drivers that are (GxEPDDisplay) — this header is
  // deliberately free of Arduino, and millis()/delay() are not available here.
  virtual void waitRefreshIdle(uint32_t max_ms = 12000) { (void)max_ms; }
#endif
  virtual bool isOn() = 0;
  virtual void turnOn() = 0;
  virtual void turnOff() = 0;
  virtual void clear() = 0;
  virtual void startFrame(ColorVal bkg = UIColor::window_bkg) = 0;
  virtual void setTextSize(int sz) = 0;
  virtual void setColor(ColorVal c) = 0;
  virtual void setCursor(int x, int y) = 0;
  virtual void print(const char* str) = 0;
  virtual void printWordWrap(const char* str, int max_width) {
#ifdef CYRILLIC_SUPPORT
    // glcdfont6x8 is monospace: 6px wide, 8px tall per glyph at GFX scale _font_size
    const int char_w    = 6 * _font_size;
    const int line_h    = 8 * _font_size;
    const int max_chars = max_width / char_w;
    // line_buf must hold max_chars Unicode chars; Cyrillic is 2 bytes each + NUL
    char line_buf[192];

    int byte_pos = 0;
    int total_bytes = (int)strlen(str);

    while (byte_pos < total_bytes && _cursor_y_raw + line_h <= height()) {
      // Count chars remaining from byte_pos
      int chars_remaining = 0;
      for (int i = byte_pos; str[i]; i++)
        if ((str[i] & 0xC0) != 0x80) chars_remaining++;

      if (chars_remaining <= max_chars) {
        print(str + byte_pos);
        break;
      }

      // Scan forward max_chars chars, track last space position
      int bi = byte_pos, sc = 0, last_sp_byte = -1;
      while (str[bi] && sc < max_chars) {
        if (str[bi] == ' ') last_sp_byte = bi;
        if ((str[bi] & 0xC0) != 0x80) sc++;
        bi++;
      }

      int break_byte = (last_sp_byte >= 0) ? last_sp_byte : bi;
      int seg = break_byte - byte_pos;
      if (seg >= (int)sizeof(line_buf)) seg = (int)sizeof(line_buf) - 1;
      memcpy(line_buf, str + byte_pos, seg);
      line_buf[seg] = 0;
      print(line_buf);

      byte_pos = break_byte + (str[break_byte] == ' ' ? 1 : 0);
      _cursor_y_raw += line_h;
      if (_cursor_y_raw + line_h <= height()) setCursor(0, _cursor_y_raw);
    }
#else
    print(str);
#endif
  }
  virtual void fillRect(int x, int y, int w, int h) = 0;
  virtual void drawRect(int x, int y, int w, int h) = 0;
  virtual void drawXbm(int x, int y, const uint8_t* bits, int w, int h) = 0;
  virtual uint16_t getTextWidth(const char* str) = 0;
  virtual void drawTextCentered(int mid_x, int y, const char* str) {   // helper method (override to optimise)
    int w = getTextWidth(str);
    setCursor(mid_x - w/2, y);
    print(str);
  }
  // Ink-centered variant for fixed-cell digit strings (e.g. "17", "08:42"):
  // centers on the glyphs' actual lit pixels rather than their advance-width
  // box. Default: identical to drawTextCentered() — override where the active
  // font's glyphs leave built-in blank space inside their advance cell.
  virtual void drawDigitsCentered(int mid_x, int y, const char* str) {
    drawTextCentered(mid_x, y, str);
  }
  // How tall drawDigitsCentered() will actually paint for a requested clock-mode
  // cap height (setTextSize() with sz > 4). Drivers quantise the request to whole
  // font scales, so what gets painted is NOT reliably cap_px rows: it can be
  // shorter, and with non-linear font tables it can be TALLER than asked. Layout
  // that budgets cap_px rows without asking is how the clock digits ended up
  // running into the widget row below them. Default: honour the request, which
  // is right for drivers that scale continuously.
  virtual int getDigitsH(int cap_px) const { return cap_px; }
  virtual void drawTextRightAlign(int x_anch, int y, const char* str) {
    int w = getTextWidth(str);
    setCursor(x_anch - w, y);
    print(str);
  }
  virtual void drawTextLeftAlign(int x_anch, int y, const char* str) {
    setCursor(x_anch, y);
    print(str);
  }
  
  // Convert UTF-8 string to a single-byte encoding suitable for the active font.
  // With CYRILLIC_SUPPORT: maps Cyrillic UTF-8 sequences to CP1251 code points used
  // by glcdfont6x8; unmapped non-ASCII bytes become 0xAE (CP1251 smile ®).
  // Without CYRILLIC_SUPPORT: replaces every non-ASCII byte with 0xDB (CP437 block █).
  virtual void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) {
    size_t j = 0;
#ifdef CYRILLIC_SUPPORT
    char lead = 0;
    char cc = 0;
#endif
    for (size_t i = 0; src[i] != 0 && j < dest_size - 1; i++) {
      unsigned char c = (unsigned char)src[i];
      if (c >= 32 && c <= 126) {
#ifdef CYRILLIC_SUPPORT
        lead = 0;
        dest[j++] = c;
      } else if (c == 0xC2 || c == 0xC3 || c == 0xD0 || c == 0xD1 || c == 0xD2) {
        // UTF-8 two-byte Cyrillic lead byte — consume continuation byte
        lead = (char)c;
        cc = 0;
        c = (unsigned char)src[++i];
        if (c != 0) {
          switch (lead) {
            case 0xC3: cc = (char)(c | 0xC0); break;
            case 0xD0:
              if      (c == 129) cc = 168;           // Ё
              else if (c == 132) cc = 170;            // Є
              else if (c == 134) cc = 178;            // І
              else if (c == 135) cc = 175;            // Ї
              else if (c > 143 && c < 192) cc = (char)(c + 48);  // А-Я
              break;
            case 0xD1:
              if      (c == 145) cc = 184;            // ё
              else if (c == 148) cc = 186;            // є
              else if (c == 150) cc = 179;            // і
              else if (c == 151) cc = 191;            // ї
              else if (c > 127 && c < 144) cc = (char)(c + 112); // а-я
              break;
            case 0xD2:
              if      (c == 144) cc = 165;            // Ґ
              else if (c == 145) cc = 180;            // ґ
              break;
          }
          dest[j++] = (cc != 0) ? cc : '\xAE';  // fallback: ® smile in CP1251
        }
      } else if (c >= 0x80) {
        lead = 0;
        dest[j++] = '\xAE';  // non-Cyrillic non-ASCII: smile placeholder
#else
        dest[j++] = c;  // ASCII printable
      } else if (c >= 0x80) {
        dest[j++] = '\xDB';  // CP437 full block █
#endif
        while (src[i+1] && (src[i+1] & 0xC0) == 0x80)
          i++;  // skip UTF-8 continuation bytes
      }
    }
    dest[j] = 0;
  }
  
  // draw text with ellipsis if it exceeds max_width
  virtual void drawTextEllipsized(int x, int y, int max_width, const char* str) {
    char temp_str[256];  // reasonable buffer size
    size_t len = strlen(str);
    if (len >= sizeof(temp_str)) len = sizeof(temp_str) - 1;
    memcpy(temp_str, str, len);
    temp_str[len] = 0;
    
    if (getTextWidth(temp_str) <= max_width) {
      setCursor(x, y);
      print(temp_str);
      return;
    }
    
    // for variable-width fonts (GxEPD), add space after ellipsis
    // for fixed-width fonts (OLED), keep tight spacing to save precious characters
    const char* ellipsis;
    // use a simple heuristic: if 'i' and 'l' have different widths, it's variable-width
    int i_width = getTextWidth("i");
    int l_width = getTextWidth("l");
    if (i_width != l_width) {
      ellipsis = "... ";  // variable-width fonts: add space
    } else {
      ellipsis = "...";   // fixed-width fonts: no space
    }
    
    int ellipsis_width = getTextWidth(ellipsis);
    int str_len = strlen(temp_str);
    
    while (str_len > 0 && getTextWidth(temp_str) > max_width - ellipsis_width) {
      temp_str[--str_len] = 0;
    }
    strcat(temp_str, ellipsis);
    
    setCursor(x, y);
    print(temp_str);
  }
  
  virtual void endFrame() = 0;
};

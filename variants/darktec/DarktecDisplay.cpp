#ifdef DISPLAY_CLASS

#include "DarktecDisplay.h"
#include "DarktecBoard.h"

extern DarktecBoard board;

bool DarktecDisplay::statusDotWanted() const {
  // Только эфир LoRa: TX, приём пакета, короткий хвост после. Не BLE.
  return board.isLoRaActivity();
}

static const int kDotX = 3;
static const int kDotS = 3;
static const int kDotY = (64 - kDotS) / 2;  // 30, середина 64px OLED; X не трогаем

void DarktecDisplay::paintStatusDot(bool on) {
  setColor(on ? UIColor::title_txt : UIColor::window_bkg);
  fillRect(kDotX, kDotY, kDotS, kDotS);
}

void DarktecDisplay::startFrame(ColorVal bkg) {
  _in_frame = true;
  _main_ui = false;
  SSD1306Display::startFrame(bkg);
}

void DarktecDisplay::drawRect(int x, int y, int w, int h) {
  // ui-new renderBatteryIndicator: 24×10 в (width-24-5, 0) — только home с шапкой.
  if (y == 0 && w == 24 && h == 10 && x == width() - 24 - 5) {
    _main_ui = true;
  }
  SSD1306Display::drawRect(x, y, w, h);
}

bool DarktecDisplay::isOn() {
  bool on = SSD1306Display::isOn();
  bool charging = board.isCharging();
  // KEEP_DISPLAY_ON_USB не будит погашенный OLED — только не даёт ему заснуть.
  if (charging && !_was_charging && !on) {
    turnOn();
    on = true;
  }
  _was_charging = charging;

  // Между кадрами UI (home ~5 с) доталкиваем только 3×3, буфер Adafruit живой.
  if (on && !_in_frame && _main_ui) {
    bool want = statusDotWanted();
    if (want != _dot_shown) {
      _dot_shown = want;
      paintStatusDot(want);
      SSD1306Display::endFrame();
    }
  }
  return on;
}

void DarktecDisplay::endFrame() {
  if (_main_ui) {
    // Молнию внутри иконки батареи НЕ рисуем: индикатор заряда — один, слева
    // от значка (его ставит ui-new renderBatteryIndicator). Здесь только
    // доталкиваем точку активности эфира между кадрами UI.
    _dot_shown = statusDotWanted();
    if (_dot_shown) {
      paintStatusDot(true);
    }
  } else {
    _dot_shown = false;
  }

  SSD1306Display::endFrame();
  _in_frame = false;
}

#endif

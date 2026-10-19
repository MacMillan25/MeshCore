#ifdef DISPLAY_CLASS

#include "DarktecDisplay.h"
#include "DarktecBoard.h"

extern DarktecBoard board;

bool DarktecDisplay::isOn() {
  bool on = SSD1306Display::isOn();
  // KEEP_DISPLAY_ON_USB не будит погашенный OLED — только не даёт ему заснуть,
  // а начало зарядки экран включает: иначе на зарядке нода выглядит мёртвой.
  bool charging = board.isCharging();
  if (charging && !_was_charging && !on) {
    turnOn();
    on = true;
  }
  _was_charging = charging;
  return on;
}

#endif

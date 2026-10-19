#include "PowerScreens.h"

#include <Arduino.h>
#include <helpers/BatteryPack.h>
#include <helpers/LowBattGuard.h>

// Power off by voltage: the level the node still runs at reliably, under its
// transmit load, is what matters, not the charge left. 3.40 V until bench
// measurements give a per-board figure; a board may set its own.
#ifndef AUTO_SHUTDOWN_MILLIVOLTS
  #define AUTO_SHUTDOWN_MILLIVOLTS  3400
#endif
// Optional extra rule by charge left (BatteryPack curve); off by default.
#ifndef AUTO_SHUTDOWN_PERCENT
  #define AUTO_SHUTDOWN_PERCENT  0
#endif

#include "UILang.h"

#ifndef POWER_ON_HINT
  #define POWER_ON_HINT _TR("Press RST to turn on", "Нажмите RST")
#endif
// After a low-battery stop: boards with nRF52 power management wake by
// themselves when USB power comes (see NRF52Board::configureVoltageWake).
#ifndef CHARGE_HINT
  #if defined(NRF52_POWER_MANAGEMENT)
    #define CHARGE_HINT _TR("Plug in USB to turn on", "Подключите USB")
  #else
    #define CHARGE_HINT _TR("Charge, then press RST", "Зарядите, затем RST")
  #endif
#endif

namespace power_screens {

void drawBoot(DisplayDriver* d) {
  if (!d) return;
  d->startFrame();
#ifdef ST7789
  d->setTextSize(2);
#endif
  d->drawTextCentered(d->width() / 2, 28, _TR("Loading...", "Загрузка..."));
  d->endFrame();
}

void drawPowerOff(DisplayDriver* d, bool low_battery) {
  if (!d) return;
  if (!d->isOn()) d->turnOn();
  const char* hint = low_battery ? CHARGE_HINT : POWER_ON_HINT;
  const int big = (d->width() >= 200) ? 2 : 1;
  const int lh  = d->getLineH(1);
  const int bh  = d->getLineH(big);
  const int total = (low_battery ? lh + 2 : 0) + bh + 4 + lh;
  int y = (d->height() - total) / 2;
  if (y < 0) y = 0;
  d->startFrame();
  d->setColor(UIColor::primary_txt);
  d->setTextSize(1);
  if (low_battery) { d->drawTextCentered(d->width() / 2, y, _TR("Low battery", "Низкий заряд")); y += lh + 2; }
  d->setTextSize(big);
  d->drawTextCentered(d->width() / 2, y, _TR("Turned off", "Выключен"));
  y += bh + 4;
  d->setTextSize(1);
  d->drawTextCentered(d->width() / 2, y, hint);
  d->endFrame();
  if (d->isEink()) {
    d->fullRefreshAndHibernate();   // keep the frame on the panel
  } else {
    delay(3000);                    // a lit screen goes dark with the node
  }
}

bool batteryFlat(uint16_t mv, bool external_power) {
  if (external_power) return false;

  // Порог можно опустить на ходу (CLI `set darktec.lowbat`), чтобы гонять
  // стенд на пониженном питании. Штатное правило «ниже 2.5 В — значит батареи
  // нет» действует только при штатном пороге: если порог задан вручную,
  // верим ему, иначе такая правка ничего бы не меняла.
  if (LowBattGuard::overridden()) {
    if (AUTO_SHUTDOWN_PERCENT > 0 && BatteryPack::getPercent(mv) <= AUTO_SHUTDOWN_PERCENT) return true;
    return mv < LowBattGuard::millivolts();
  }

  if (mv < 2500) return false;
  if (AUTO_SHUTDOWN_PERCENT > 0 && BatteryPack::getPercent(mv) <= AUTO_SHUTDOWN_PERCENT) return true;
  return mv < AUTO_SHUTDOWN_MILLIVOLTS;
}

}  // namespace power_screens

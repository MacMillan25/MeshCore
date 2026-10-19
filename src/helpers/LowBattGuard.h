#pragma once

#include <stdint.h>

// Порог аварийного отключения по низкому напряжению батареи.
//
// Штатное значение задаёт `AUTO_SHUTDOWN_MILLIVOLTS` в `PowerScreens.cpp`
// (3.40 В — уровень, на котором нода ещё держится под нагрузкой передачи).
// Для стенда с лабораторным БП этого много: нода выключается раньше, чем
// заканчивается тест. CLI-команда `set darktec.lowbat <вольты>` опускает порог
// на ходу.
//
// Значение намеренно НЕ пишется в prefs: после перезагрузки защита возвращается
// к штатной. Иначе легко забыть про неё и уехать с «китайской» прошивкой,
// которая спокойно разряжает банку в ноль.
namespace LowBattGuard {

inline uint16_t& overrideMv() {
  static uint16_t mv = 0;   // 0 — переопределения нет
  return mv;
}

// 0 — работает штатный порог.
inline uint16_t millivolts() { return overrideMv(); }
inline bool overridden() { return overrideMv() != 0; }
inline void set(uint16_t mv) { overrideMv() = mv; }
inline void clear() { overrideMv() = 0; }

}  // namespace LowBattGuard

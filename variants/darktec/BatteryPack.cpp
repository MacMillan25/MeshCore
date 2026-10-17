// Оверлей helpers/BatteryPack.cpp для варианта Darktec.
// API тот же, что у ядра; отличие: SOC берётся из НАШЕЙ модели питания
// (DarktecPowerModel: кривая разряда, R-компенсация заряда, авто-полярность),
// а не из generic-таблицы ядра. Фасад — DarktecPowerState (DarktecBoard.cpp).

#include <helpers/BatteryPack.h>

#if defined(ARDUINO)
  #include <Arduino.h>
#endif

#include "battery_chemistry.h"
#include "DarktecPowerState.h"

namespace {

uint8_t linearPercent(uint16_t pack_mv, uint8_t cells) {
  cells = cells < 1 ? 1 : cells;
  const uint32_t cell_mv = pack_mv / cells;
  constexpr uint32_t kMin = 3000, kMax = 4200;
  if (cell_mv >= kMax) return 100;
  if (cell_mv <= kMin) return 0;
  return (uint8_t)((cell_mv - kMin) * 100 / (kMax - kMin));
}

uint8_t seriesCellsOrDefault(uint8_t series_cells) {
  uint8_t cells = series_cells ? series_cells : (uint8_t)BATT_SERIES_CELLS;
  return cells < 1 ? 1 : cells;
}

}  // namespace

namespace BatteryPack {

const BatteryProfile& profileFor(BatteryChemistry chem) {
  // Профили нужны только для совместимости API ядра; реальный расчёт —
  // в DarktecPowerState/DarktecPowerModel.
  static const BatteryProfile empty{BatteryChemistry::Custom, 1, nullptr, 0};
  (void)chem;
  return empty;
}

const BatteryProfile& activeProfile() {
  return profileFor(static_cast<BatteryChemistry>((uint8_t)BATT_CHEMISTRY));
}

uint8_t getPercent(uint16_t pack_mv, BatteryChemistry chem, uint8_t series_cells) {
  // Кривая DarkTec снята для Li-ion 1S; для прочих химий/ячеек — линейная
  // оценка, чтобы API остался работоспособным.
  if (chem == BatteryChemistry::Lithium && seriesCellsOrDefault(series_cells) == 1) {
    return DarktecPowerState::socPercent(pack_mv);
  }
  return linearPercent(pack_mv, seriesCellsOrDefault(series_cells));
}

uint8_t getPercent(uint16_t pack_mv, BatteryChemistry chem) {
  return getPercent(pack_mv, chem, 0);
}

// Активный путь (иконка батареи, телеметрия): SOC нашей модели питания.
uint8_t getPercent(uint16_t pack_mv) {
  return DarktecPowerState::socPercent(pack_mv);
}

uint16_t fullMilliVolts() {
  // Верхняя точка пакета для нашей модели: BATTERY_CHEMISTRY задаёт её
  // в battery_chemistry.h (Li-ion 4200, LiFePO4 3650, LTO 2500).
  return (uint16_t)BATT_MAX_MILLIVOLTS;
}

// ---- Кэш напряжения пакета (INA-подпитка) — поведение ядра, 1:1 ----

static uint16_t s_pack_mv = 0;
static uint32_t s_pack_ms = 0;
static bool s_pack_valid = false;

static uint32_t s_test_clock_ms = 0;
static uint32_t (*s_clock_ms)() = nullptr;

static uint32_t nowMs() {
  if (s_clock_ms) return s_clock_ms();
#if defined(ARDUINO)
  return millis();
#else
  return s_test_clock_ms;
#endif
}

void setClockMs(uint32_t (*clock_ms)()) { s_clock_ms = clock_ms; }

void setClockMsValue(uint32_t ms) { s_test_clock_ms = ms; }

void notePackMilliVolts(uint16_t mv) {
  if (mv < 500 || mv > 20000) return;
  s_pack_mv = mv;
  s_pack_ms = nowMs();
  s_pack_valid = true;
}

void clearPackMilliVolts() {
  s_pack_valid = false;
  s_pack_mv = 0;
  s_pack_ms = 0;
}

bool hasFreshPackMilliVolts(uint16_t& out_mv) {
  if (!s_pack_valid) return false;
  const uint32_t age = nowMs() - s_pack_ms;
  if (age > BATT_PACK_INA_TTL_MS) return false;
  out_mv = s_pack_mv;
  return true;
}

}  // namespace BatteryPack

// Оверлей helpers/BatteryPack.cpp для варианта Darktec (spec 0001, FR-25).
// API тот же, что у ядра; отличие: getPercent(pack_mv) возвращает SOC из
// модели DarktecSocModel (кулонометр + таблица замера + R-компенсация),
// а не из generic-таблицы ядра. Ядро при апдейте сюда не «приносит» свои
// таблицы — состояние питания Darktec живёт в DarktecBoard.cpp
// (DarktecPowerState). Износ: при изменении API ядра этот файл правим
// осознанно (см. spec 0001, NFR-2).

#include <helpers/BatteryPack.h>

#if defined(ARDUINO)
  #include <Arduino.h>
#endif

#include "DarktecPrefs.h"
#include "DarktecSocModel.h"
#include "DarktecPowerState.h"
#include "DarktecSoc.h"

namespace {

// Не используемые Darktec химии — копия таблиц ядра (совместимость API).
// Descending voltage. 1S cell mV → %.
const BatteryOcvPoint kNiMH[] = {
  {1400, 100}, {1350, 90}, {1300, 70}, {1250, 40},
  {1200,  20}, {1100, 10}, {1000,  0},
};
const BatteryOcvPoint kLeadAcid[] = {
  {2150, 100}, {2100, 90}, {2050, 70}, {2000, 40},
  {1950,  20}, {1900, 10}, {1800,  0},
};

uint8_t seriesCellsOrDefault(uint8_t series_cells) {
  uint8_t cells = series_cells ? series_cells : (uint8_t)BATT_SERIES_CELLS;
  return cells < 1 ? 1 : cells;
}

// SOC по нашим таблицам (DarktecSoc) для поддерживаемых химий.
uint8_t darktecPercent(uint16_t pack_mv, DarktecSoc::Chem chem, uint8_t cells) {
  const uint16_t cell_mv = (uint16_t)(pack_mv / (cells < 1 ? 1 : cells));
  return DarktecSoc::socFromCellMv(chem, cell_mv);
}

}  // namespace

namespace BatteryPack {

const BatteryProfile& profileFor(BatteryChemistry chem) {
  // Профили нужны только для совместимости API (никто вне ядра не вызывает).
  // Отдаём статический пустой профиль — реальный расчёт идёт через
  // DarktecSoc/DarktecPowerState (см. getPercent ниже).
  static const BatteryProfile empty{BatteryChemistry::Custom, 1, nullptr, 0};
  (void)chem;
  return empty;
}

const BatteryProfile& activeProfile() {
  return profileFor(static_cast<BatteryChemistry>((uint8_t)BATT_CHEMISTRY));
}

uint8_t getPercent(uint16_t pack_mv, BatteryChemistry chem, uint8_t series_cells) {
  const uint8_t cells = seriesCellsOrDefault(series_cells);
  switch (chem) {
    case BatteryChemistry::Lithium:
      return darktecPercent(pack_mv, DarktecSoc::Chem::LiIon, cells);
    case BatteryChemistry::LiFePO4:
      return darktecPercent(pack_mv, DarktecSoc::Chem::LiFePO4, cells);
    case BatteryChemistry::NiMH: {
      // Ядро-таблица (в darktec-ролях не используется).
      static const BatteryOcvPoint pts[] = {
        {1400, 100}, {1350, 90}, {1300, 70}, {1250, 40},
        {1200,  20}, {1100, 10}, {1000,  0},
      };
      const uint16_t cell_mv = (uint16_t)(pack_mv / cells);
      if (cell_mv >= pts[0].mv) return 100;
      if (cell_mv <= pts[6].mv) return 0;
      for (uint8_t i = 0; i + 1 < 7; i++) {
        if (cell_mv <= pts[i].mv && cell_mv >= pts[i + 1].mv) {
          return (uint8_t)(pts[i + 1].pct +
                           ((uint32_t)(cell_mv - pts[i + 1].mv) * (pts[i].pct - pts[i + 1].pct)) /
                               (pts[i].mv - pts[i + 1].mv));
        }
      }
      return 0;
    }
    case BatteryChemistry::LeadAcid: {
      static const BatteryOcvPoint pts[] = {
        {2150, 100}, {2100, 90}, {2050, 70}, {2000, 40},
        {1950,  20}, {1900, 10}, {1800,  0},
      };
      const uint16_t cell_mv = (uint16_t)(pack_mv / cells);
      if (cell_mv >= pts[0].mv) return 100;
      if (cell_mv <= pts[6].mv) return 0;
      for (uint8_t i = 0; i + 1 < 7; i++) {
        if (cell_mv <= pts[i].mv && cell_mv >= pts[i + 1].mv) {
          return (uint8_t)(pts[i + 1].pct +
                           ((uint32_t)(cell_mv - pts[i + 1].mv) * (pts[i].pct - pts[i + 1].pct)) /
                               (pts[i].mv - pts[i + 1].mv));
        }
      }
      return 0;
    }
    default:
      return darktecPercent(pack_mv, DarktecSoc::Chem::LiIon, cells);
  }
}

uint8_t getPercent(uint16_t pack_mv, BatteryChemistry chem) {
  return getPercent(pack_mv, chem, 0);
}

// Активный путь (экран/телеметрия юга): SOC из модели питания Darktec.
uint8_t getPercent(uint16_t pack_mv) {
  return DarktecPowerState::socPercent(pack_mv);
}

uint16_t fullMilliVolts() {
  // Верхняя точка таблицы активной химии × ячейки (химия — из настроек).
  const auto& prefs = DarktecPrefs::get();
  const DarktecSoc::Chem chem =
      (prefs.chem == DarktecPrefsFormat::CHEM_LIFEPO4) ? DarktecSoc::Chem::LiFePO4
      : (prefs.chem == DarktecPrefsFormat::CHEM_LTO)   ? DarktecSoc::Chem::LTO
                                                       : DarktecSoc::Chem::LiIon;
  const uint16_t top = DarktecSocModel::fullCellMv(chem);
  return (uint16_t)(top * (prefs.cells < 1 ? 1 : prefs.cells));
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
  // Игнорируем очевидный мусор (обрыв / КЗ / чужая шина).
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

#pragma once
// Формат файла настроек DarkTec `/darktec.prefs`.
// Чистый (без Arduino) — подключается и в native-тестах.
//
// Что здесь живёт: то, что относится к железу DarkTec и не имеет смысла
// для других плат — модель аккумулятора (ёмкость), впаянный радиомодуль,
// яркость экрана и две настройки экранов. Остальное (язык, часы,
// сообщения) — в NodePrefs ядра.
//
// Формат бинарный намеренно: файл отдельный от /prefs.json, чтобы правка
// настроек DarkTec не переписывала ядровые.

#include <stddef.h>
#include <stdint.h>

#include "battery_chemistry.h"

namespace DarktecPrefsFormat {

// Индексы радиомодулей. В сборке под LR2021 модуль больше не выбирается:
// узел один, и этот байт остался только ради совместимости формата v4 —
// он читается, проверяется и пишется, но ни на что не влияет.
//   0 E22-900M33S, 1 E22-900M30S, 2 HT-RA62, 3 E22P-868M30S.
enum ModuleCode : uint8_t {
  MOD_E22_900M33S = 0,
  MOD_E22_900M30S = 1,
  MOD_HT_RA62 = 2,
  MOD_E22P_868M30S = 3,
};

struct Settings {
  uint16_t capacity_mah;  // ёмкость пакета для режима UserPref, mAh (1000..10000)
  uint8_t module;         // ModuleCode (не используется, см. комментарий выше)
  uint8_t auto_off_idx;   // 0=15s, 1=30s, 2=1m, 3=5m, 4=1h
  uint8_t noise_idx;      // 0=100ms, 1=250ms, 2=500ms, 3=1000ms (в памяти, не в файле)
  uint8_t capacity_mode;  // CapacityMode: 0=UserPref, 1=3000, 2=4000, 3=RealCap
  uint8_t brightness;     // BrightnessLevel: 0=Dim, 1=Normal, 2=Bright
  uint8_t bar_value;      // BarValue: что показывать в мини-баре заставки
  uint8_t low_batt_mv100; // порог отключения по низкому напряжению, в 100 мВ; 0 = штатный 3.40 В
  uint32_t factory_crc;   // CRC32 применённого блока заводских настроек (0 = не применяли)
  uint16_t real_cap_mah;  // измеренная ёмкость пакета, mAh (0 = ещё не измеряли)
  uint32_t chrg_end_epoch; // Unix-время конца последней зарядки (0 = неизвестно)
  uint8_t real_cap_low_idx; // нижняя точка старта RealCap: 0=3.0, 1=3.2, 2=3.4 В
  uint8_t saw_empty;       // 1 = до порога разрядились, цикл RealCap начат (живёт в prefs)
};

// Что показывать в мини-баре заставки «Always On».
enum BarValue : uint8_t {
  BAR_NF = 0,        // шумовой пол
  BAR_PERCENT = 1,   // процент заряда
};
constexpr uint8_t kBarValueCount = 2;

// Откуда брать ёмкость. В меню листаются четыре пункта в порядке
// kCapacityOrder: RealCap → пользовательская (capacity_mah, её задаёт
// прошивальщик) → 4000 → 3000. Значения enum — это то, что лежит в файле,
// поэтому они стабильны и не совпадают с порядком в меню.
enum CapacityMode : uint8_t {
  CAP_USER = 0,
  CAP_3000 = 1,
  CAP_4000 = 2,
  CAP_REAL = 3,   // измеренная ёмкость пакета (RealCap)
};
constexpr uint8_t kCapacityModeCount = 4;

// Порядок листания в меню и в CLI.
inline const uint8_t* capacityOrder() {
  static const uint8_t kOrder[kCapacityModeCount] = { CAP_REAL, CAP_USER, CAP_4000, CAP_3000 };
  return kOrder;
}

// Яркость экрана: три уровня, в меню называются словами, а не цифрами.
// Значение — это аппаратный контраст SSD1306 (команда 0x81, 0..255).
// 1, а не 0: при нулевом контрасте экран не читается вообще.
enum BrightnessLevel : uint8_t {
  BRI_LOW = 0,      // 1
  BRI_MID = 1,      // 127
  BRI_HIGH = 2,     // 255
};
constexpr uint8_t kBrightnessCount = 3;

inline uint8_t brightnessContrast(uint8_t level) {
  static const uint8_t kValues[kBrightnessCount] = { 1, 127, 255 };
  return (level < kBrightnessCount) ? kValues[level] : 255;
}

constexpr uint16_t kCapacityMin = 1000;
constexpr uint16_t kCapacityMax = 10000;
constexpr uint16_t kCapacityDefault = 3000;

constexpr uint8_t kAutoOffCount = 5;
constexpr uint8_t kNoiseCount = 4;

// Нижняя точка, с которой начинается цикл измерения ёмкости (RealCap).
// 3.0 В — стенд; 3.4 В — бережный режим для живой банки: глубже литий
// разряжать смысла нет, а цикл всё равно засчитывается.
enum RealCapLow : uint8_t {
  RCAP_LOW_3000 = 0,
  RCAP_LOW_3200 = 1,
  RCAP_LOW_3400 = 2,
};
constexpr uint8_t kRealCapLowCount = 3;

inline uint16_t realCapLowMv(uint8_t idx) {
  static const uint16_t kMv[kRealCapLowCount] = { 3000, 3200, 3400 };
  return (idx < kRealCapLowCount) ? kMv[idx] : kMv[RCAP_LOW_3400];
}

constexpr uint8_t kWireVersion = 6;
constexpr uint8_t kWireSize = 28;  // magic(4) + ver(1) + payload(21) + crc(2)
constexpr uint8_t kPayloadSize = 21;
// У версий 1..5 полезная часть была 19 байт — их CRC считается по ней.
constexpr uint8_t kPayloadLegacy = 19;

inline void sanitize(Settings& s);

// Фактическая ёмкость пакета: RealCap, пользовательская или фиксированная.
// Режим RealCap до первого полного цикла опирается на пользовательскую
// ёмкость: подставлять 4000 вслепую — значит врать про запас на 3000-банке.
inline uint16_t effectiveCapacityMah(const Settings& s) {
  if (s.capacity_mode == CAP_3000) return 3000;
  if (s.capacity_mode == CAP_4000) return 4000;
  if (s.capacity_mode == CAP_REAL) {
    return s.real_cap_mah ? s.real_cap_mah : s.capacity_mah;
  }
  return s.capacity_mah;
}

inline void sanitize(Settings& s);

inline Settings buildFlagDefaults() {
  Settings s{};
#if defined(BATTERY_CAPACITY_MAH)
  s.capacity_mah = (uint16_t)BATTERY_CAPACITY_MAH;
#else
  s.capacity_mah = kCapacityDefault;
#endif
  s.module = MOD_E22_900M33S;
  s.auto_off_idx = 0;
  s.noise_idx = 0;  // 100 мс: скорость NF в памяти не хранится, это стартовое
  s.capacity_mode = CAP_USER;
  s.brightness = BRI_HIGH;
  s.real_cap_low_idx = RCAP_LOW_3400;   // по умолчанию бережно к банке
  sanitize(s);
  return s;
}

inline void sanitize(Settings& s) {
  if (s.capacity_mah < kCapacityMin || s.capacity_mah > kCapacityMax) {
    s.capacity_mah = kCapacityDefault;
  }
  if (s.module > MOD_E22P_868M30S) s.module = MOD_E22_900M33S;
  if (s.auto_off_idx >= kAutoOffCount) s.auto_off_idx = 0;
  if (s.noise_idx >= kNoiseCount) s.noise_idx = 0;
  if (s.capacity_mode >= kCapacityModeCount) s.capacity_mode = CAP_USER;
  if (s.brightness >= kBrightnessCount) s.brightness = BRI_HIGH;
  if (s.bar_value >= kBarValueCount) s.bar_value = BAR_NF;
  if (s.low_batt_mv100 > 40) s.low_batt_mv100 = 0;   // 0..4.0 В; больше — мусор
  if (s.real_cap_mah > 20000) s.real_cap_mah = 0;      // мусор в файле — считаем «не измерено»
  if (s.real_cap_low_idx >= kRealCapLowCount) s.real_cap_low_idx = RCAP_LOW_3400;
  s.saw_empty = s.saw_empty ? 1 : 0;
}

// CRC16/CCITT-FALSE (poly 0x1021, init 0xFFFF).
inline uint16_t crc16(const uint8_t* data, uint32_t len) {
  uint16_t crc = 0xFFFF;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

// "DRTC" + version + payload(capacity_le16, module, auto_off, noise,
// capacity_mode, brightness, bar_value, 5×0x00) + crc16(version+payload).
// bar_value занял первый резервный байт: у старых v4-файлов он нулевой, и это
// ровно BAR_NF, поэтому версию формата поднимать не пришлось.
inline size_t serialize(const Settings& s, uint8_t* buf, size_t len) {
  if (len < kWireSize) return 0;
  Settings t = s;
  sanitize(t);
  buf[0] = 'D'; buf[1] = 'R'; buf[2] = 'T'; buf[3] = 'C';
  buf[4] = kWireVersion;
  buf[5] = (uint8_t)(t.capacity_mah & 0xFF);
  buf[6] = (uint8_t)(t.capacity_mah >> 8);
  buf[7] = t.module;
  buf[8] = t.auto_off_idx;
  buf[9] = t.noise_idx;
  buf[10] = t.capacity_mode;
  buf[11] = t.brightness;
  buf[12] = t.bar_value;
  buf[13] = t.low_batt_mv100;
  // Байты 14..17 — CRC32 применённого блока заводских настроек.
  buf[14] = (uint8_t)(t.factory_crc & 0xFF);
  buf[15] = (uint8_t)((t.factory_crc >> 8) & 0xFF);
  buf[16] = (uint8_t)((t.factory_crc >> 16) & 0xFF);
  buf[17] = (uint8_t)((t.factory_crc >> 24) & 0xFF);
  const uint16_t crc = crc16(buf + 4, 1 + kPayloadSize);
  // v5: измеренная ёмкость и время конца последней зарядки.
  buf[18] = (uint8_t)(t.real_cap_mah & 0xFF);
  buf[19] = (uint8_t)(t.real_cap_mah >> 8);
  buf[20] = (uint8_t)(t.chrg_end_epoch & 0xFF);
  buf[21] = (uint8_t)((t.chrg_end_epoch >> 8) & 0xFF);
  buf[22] = (uint8_t)((t.chrg_end_epoch >> 16) & 0xFF);
  buf[23] = (uint8_t)((t.chrg_end_epoch >> 24) & 0xFF);
  // v6: нижняя точка старта RealCap и отметка «разрядились до неё».
  // Отметка живёт в файле намеренно: на 3.4 В нода может выключиться раньше,
  // чем начнётся заряд, и без неё цикл после перезагрузки начинался бы заново.
  buf[24] = t.real_cap_low_idx;
  buf[25] = t.saw_empty;
  const uint16_t crc2 = crc16(buf + 4, 1 + kPayloadSize);
  buf[26] = (uint8_t)(crc2 & 0xFF);
  buf[27] = (uint8_t)(crc2 >> 8);
  return kWireSize;
}

// v1 (прошивки beeline 1.18.0): capacity(2), chem, cells, module, charge(2).
// Порядок модулей был другим (0 RA62, 1 30S, 2 E22P, 3 33S) — переводим.
inline bool parseV1(const uint8_t* buf, size_t len, Settings& out) {
  if (len < 20) return false;
  const uint16_t crc_stored = (uint16_t)(buf[18] | ((uint16_t)buf[19] << 8));
  if (crc16(buf + 4, 1 + kPayloadLegacy) != crc_stored) return false;
  Settings s{};
  s.capacity_mah = (uint16_t)(buf[5] | ((uint16_t)buf[6] << 8));
  const uint8_t old_module = buf[9];
  static const uint8_t kMap[4] = { MOD_HT_RA62, MOD_E22_900M30S,
                                   MOD_E22P_868M30S, MOD_E22_900M33S };
  s.module = (old_module <= 3) ? kMap[old_module] : MOD_E22_900M33S;
  s.auto_off_idx = 0;
  s.noise_idx = 2;
  s.capacity_mode = CAP_USER;
  s.brightness = BRI_HIGH;
  sanitize(s);
  out = s;
  return true;
}

// v2 (наш первый перенос): те же байты, но резерв вместо режима ёмкости.
// Режим всегда UserPref — старая ёмкость 3000/4000 и была пользовательской.
inline bool parseV2(const uint8_t* buf, size_t len, Settings& out) {
  if (len < 20) return false;
  const uint16_t crc_stored = (uint16_t)(buf[18] | ((uint16_t)buf[19] << 8));
  if (crc16(buf + 4, 1 + kPayloadLegacy) != crc_stored) return false;
  Settings s{};
  s.capacity_mah = (uint16_t)(buf[5] | ((uint16_t)buf[6] << 8));
  s.module = buf[7];
  s.auto_off_idx = buf[8];
  s.noise_idx = buf[9];
  s.capacity_mode = CAP_USER;
  s.brightness = BRI_HIGH;
  sanitize(s);
  out = s;
  return true;
}

// v3: появился режим ёмкости, яркости ещё не было — ставим максимальную.
inline bool parseV3(const uint8_t* buf, size_t len, Settings& out) {
  if (len < 20) return false;
  const uint16_t crc_stored = (uint16_t)(buf[18] | ((uint16_t)buf[19] << 8));
  if (crc16(buf + 4, 1 + kPayloadLegacy) != crc_stored) return false;
  Settings s{};
  s.capacity_mah = (uint16_t)(buf[5] | ((uint16_t)buf[6] << 8));
  s.module = buf[7];
  s.auto_off_idx = buf[8];
  s.noise_idx = buf[9];
  s.capacity_mode = buf[10];
  s.brightness = BRI_HIGH;
  sanitize(s);
  out = s;
  return true;
}

inline bool parseV4(const uint8_t* buf, size_t len, Settings& out) {
  if (len < 20) return false;
  const uint16_t crc_stored = (uint16_t)(buf[18] | ((uint16_t)buf[19] << 8));
  if (crc16(buf + 4, 1 + kPayloadLegacy) != crc_stored) return false;
  Settings s{};
  s.capacity_mah = (uint16_t)(buf[5] | ((uint16_t)buf[6] << 8));
  s.module = buf[7];
  s.auto_off_idx = buf[8];
  s.noise_idx = buf[9];
  s.capacity_mode = buf[10];
  s.brightness = buf[11];
  s.bar_value = buf[12];
  s.low_batt_mv100 = buf[13];
  s.factory_crc = (uint32_t)buf[14] | ((uint32_t)buf[15] << 8) |
                  ((uint32_t)buf[16] << 16) | ((uint32_t)buf[17] << 24);
  sanitize(s);
  out = s;
  return true;
}

inline bool parseV5(const uint8_t* buf, size_t len, Settings& out) {
  if (len < 20) return false;
  const uint16_t crc_stored = (uint16_t)(buf[24] | ((uint16_t)buf[25] << 8));
  if (crc16(buf + 4, 1 + kPayloadLegacy) != crc_stored) return false;
  Settings s{};
  s.capacity_mah = (uint16_t)(buf[5] | ((uint16_t)buf[6] << 8));
  s.module = buf[7];
  s.auto_off_idx = buf[8];
  s.noise_idx = buf[9];
  s.capacity_mode = buf[10];
  s.brightness = buf[11];
  s.bar_value = buf[12];
  s.low_batt_mv100 = buf[13];
  s.factory_crc = (uint32_t)buf[14] | ((uint32_t)buf[15] << 8) |
                  ((uint32_t)buf[16] << 16) | ((uint32_t)buf[17] << 24);
  s.real_cap_mah = (uint16_t)(buf[18] | ((uint16_t)buf[19] << 8));
  s.chrg_end_epoch = (uint32_t)buf[20] | ((uint32_t)buf[21] << 8) |
                     ((uint32_t)buf[22] << 16) | ((uint32_t)buf[23] << 24);
  sanitize(s);
  out = s;
  return true;
}

// v6: добавились нижняя точка старта RealCap и отметка «разрядились до неё».
inline bool parseV6(const uint8_t* buf, size_t len, Settings& out) {
  if (len < kWireSize) return false;
  const uint16_t crc_stored = (uint16_t)(buf[26] | ((uint16_t)buf[27] << 8));
  if (crc16(buf + 4, 1 + kPayloadSize) != crc_stored) return false;
  Settings s{};
  s.capacity_mah = (uint16_t)(buf[5] | ((uint16_t)buf[6] << 8));
  s.module = buf[7];
  s.auto_off_idx = buf[8];
  s.noise_idx = buf[9];
  s.capacity_mode = buf[10];
  s.brightness = buf[11];
  s.bar_value = buf[12];
  s.low_batt_mv100 = buf[13];
  s.factory_crc = (uint32_t)buf[14] | ((uint32_t)buf[15] << 8) |
                  ((uint32_t)buf[16] << 16) | ((uint32_t)buf[17] << 24);
  s.real_cap_mah = (uint16_t)(buf[18] | ((uint16_t)buf[19] << 8));
  s.chrg_end_epoch = (uint32_t)buf[20] | ((uint32_t)buf[21] << 8) |
                     ((uint32_t)buf[22] << 16) | ((uint32_t)buf[23] << 24);
  s.real_cap_low_idx = buf[24];
  s.saw_empty = buf[25];
  sanitize(s);
  out = s;
  return true;
}

inline bool parse(const uint8_t* buf, size_t len, Settings& out) {
  if (len < 20) return false;
  if (buf[0] != 'D' || buf[1] != 'R' || buf[2] != 'T' || buf[3] != 'C') return false;
  switch (buf[4]) {
    case 1: return parseV1(buf, len, out);
    case 2: return parseV2(buf, len, out);
    case 3: return parseV3(buf, len, out);
    case 4: return parseV4(buf, len, out);
    case 5: return parseV5(buf, len, out);
    case kWireVersion: return parseV6(buf, len, out);
    default: return false;
  }
}

}  // namespace DarktecPrefsFormat

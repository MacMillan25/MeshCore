#pragma once
// Формат файла настроек DarkTec `/darktec.prefs`.
// Чистый (без Arduino) — подключается и в native-тестах.
//
// Что здесь живёт: то, что относится к железу DarkTec и не имеет смысла
// для других плат — модель аккумулятора (ёмкость), впаянный радиомодуль и
// две настройки экранов. Остальное (язык, часы, сообщения) — в NodePrefs ядра.
//
// Формат бинарный намеренно: файл отдельный от /prefs.json, чтобы правка
// настроек DarkTec не переписывала ядровые.

#include <stddef.h>
#include <stdint.h>

#include "battery_chemistry.h"

namespace DarktecPrefsFormat {

// Индексы радиомодулей. Порядок ОБЯЗАН совпадать с подписями в UI и с
// таблицами токов в DarktecPowerTables.h (модель берёт ток по индексу):
//   0 E22-900M33S, 1 E22-900M30S, 2 HT-RA62, 3 E22P-868M30S.
// 0..2 — пин 6 модуля это RXEN под RadioLib; 3 — EN, держится высоким вручную.
enum ModuleCode : uint8_t {
  MOD_E22_900M33S = 0,
  MOD_E22_900M30S = 1,
  MOD_HT_RA62 = 2,
  MOD_E22P_868M30S = 3,
};

struct Settings {
  uint16_t capacity_mah;  // ёмкость пакета, mAh (3000 / 4000)
  uint8_t module;         // ModuleCode
  uint8_t auto_off_idx;   // 0=15s, 1=30s, 2=1m, 3=5m, 4=1h
  uint8_t noise_idx;      // 0=100ms, 1=250ms, 2=500ms, 3=1000ms
};

constexpr uint16_t kCapacityMin = 500;
constexpr uint16_t kCapacityMax = 30000;
constexpr uint16_t kCapacityDefault = 3000;

constexpr uint8_t kAutoOffCount = 5;
constexpr uint8_t kNoiseCount = 4;

constexpr uint8_t kWireVersion = 2;
constexpr uint8_t kWireSize = 20;  // magic(4) + ver(1) + payload(13) + crc(2)
constexpr uint8_t kPayloadSize = 13;

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
  s.noise_idx = 2;  // 500 мс — как было в 1.17.1
  sanitize(s);
  return s;
}

inline void sanitize(Settings& s) {
  if (s.capacity_mah < kCapacityMin || s.capacity_mah > kCapacityMax) {
    s.capacity_mah = kCapacityDefault;
  }
  if (s.module > MOD_E22P_868M30S) s.module = MOD_E22_900M33S;
  if (s.auto_off_idx >= kAutoOffCount) s.auto_off_idx = 0;
  if (s.noise_idx >= kNoiseCount) s.noise_idx = 2;
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

// "DRTC" + version + payload(capacity_le16, module, auto_off, noise, 8×0x00)
// + crc16(version+payload).
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
  for (int i = 0; i < 8; i++) buf[10 + i] = 0x00;
  const uint16_t crc = crc16(buf + 4, 1 + kPayloadSize);
  buf[18] = (uint8_t)(crc & 0xFF);
  buf[19] = (uint8_t)(crc >> 8);
  return kWireSize;
}

// v1 (прошивки beeline 1.18.0): capacity(2), chem, cells, module, charge(2).
// Порядок модулей был другим (0 RA62, 1 30S, 2 E22P, 3 33S) — переводим.
inline bool parseV1(const uint8_t* buf, size_t len, Settings& out) {
  if (len < kWireSize) return false;
  const uint16_t crc_stored = (uint16_t)(buf[18] | ((uint16_t)buf[19] << 8));
  if (crc16(buf + 4, 1 + kPayloadSize) != crc_stored) return false;
  Settings s{};
  s.capacity_mah = (uint16_t)(buf[5] | ((uint16_t)buf[6] << 8));
  const uint8_t old_module = buf[9];
  static const uint8_t kMap[4] = { MOD_HT_RA62, MOD_E22_900M30S,
                                   MOD_E22P_868M30S, MOD_E22_900M33S };
  s.module = (old_module <= 3) ? kMap[old_module] : MOD_E22_900M33S;
  s.auto_off_idx = 0;
  s.noise_idx = 2;
  sanitize(s);
  out = s;
  return true;
}

inline bool parse(const uint8_t* buf, size_t len, Settings& out) {
  if (len < kWireSize) return false;
  if (buf[0] != 'D' || buf[1] != 'R' || buf[2] != 'T' || buf[3] != 'C') return false;
  if (buf[4] == 1) return parseV1(buf, len, out);
  if (buf[4] != kWireVersion) return false;
  const uint16_t crc_stored = (uint16_t)(buf[18] | ((uint16_t)buf[19] << 8));
  if (crc16(buf + 4, 1 + kPayloadSize) != crc_stored) return false;
  Settings s{};
  s.capacity_mah = (uint16_t)(buf[5] | ((uint16_t)buf[6] << 8));
  s.module = buf[7];
  s.auto_off_idx = buf[8];
  s.noise_idx = buf[9];
  sanitize(s);
  out = s;
  return true;
}

}  // namespace DarktecPrefsFormat

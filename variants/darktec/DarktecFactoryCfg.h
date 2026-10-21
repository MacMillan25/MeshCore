#pragma once

#include <stdint.h>

// Блок «заводских» настроек, который прошивальщик вписывает прямо в образ
// прошивки (UF2 или OTA-пакет). Прошивка на старте читает его и, если метка
// совпала и CRC сошлась, один раз переносит значения в prefs.
//
// Почему массив, а не отдельная секция линкера: массив лежит внутри образа,
// поэтому патчить его можно, просто заменив байты в UF2 — количество блоков и
// их адреса не меняются. Дописывать новый блок в конец UF2 пришлось бы с
// пересчётом numBlocks во всех блоках, а это лишний риск.
//
// Раскладка (little-endian):
//   0..15   метка "DARKTEC-FACTORY\x01" — по ней прошивальщик находит блок
//   16      версия раскладки (kVersion)
//   17      длина полезной части в байтах
//   18..19  резерв (0)
//   20..23  CRC32 (IEEE) полезной части
//   24..    полезная часть (kPayloadSize байт)
//
// Полезная часть (kVersion 1): см. Payload.
namespace DarktecFactoryCfg {

constexpr uint8_t kVersion = 1;
constexpr int kMarkerSize = 16;
constexpr int kHeaderSize = 24;
constexpr int kPayloadSize = 128;

// Магия: 15 байт текста + номер версии. В образе должна встречаться РОВНО
// один раз — по ней прошивальщик находит блок. Поэтому отдельного массива с
// меткой здесь нет: копия в .rodata дала бы второе вхождение, и патчер бы
// отказался работать (проверено на d7 — пришлось чинить).

// Что именно применять: имя и радио трогаем только если их задали, иначе
// пустое поле затирало бы настройки пользователя.
enum Flags : uint8_t {
  FLAG_NAME     = 1 << 0,
  FLAG_RADIO    = 1 << 1,   // частота, полоса, SF, CR, мощность
  FLAG_CAPACITY = 1 << 2,   // ёмкость пакета
  FLAG_MODULE   = 1 << 3,   // радиомодуль
};

struct Payload {
  uint8_t name[32];        // 0: имя ноды, 0-терминированное
  float freq;              // 32: МГц
  float bw;                // 36: кГц
  uint8_t sf;              // 40
  uint8_t cr;              // 41
  int8_t tx_dbm;           // 42
  uint8_t capacity_mode;   // 43: DarktecPrefsFormat::CapacityMode
  uint16_t capacity_mah;   // 44
  uint8_t module;          // 46: DarktecPrefsFormat::ModuleCode
  uint8_t auto_off_idx;    // 47
  uint8_t brightness;      // 48
  uint8_t bar_value;       // 49
  uint8_t low_batt_mv100;  // 50
  uint8_t flags;           // 51: Flags
  uint8_t reserved[76];    // 52..127
};
static_assert(sizeof(Payload) == kPayloadSize, "Payload должен быть 128 байт");

struct Blob {
  uint8_t marker[kMarkerSize];
  uint8_t version;
  uint8_t length;
  uint8_t reserved[2];
  uint32_t crc32;
  uint8_t payload[kPayloadSize];
};
static_assert(sizeof(Blob) == kHeaderSize + kPayloadSize, "Blob должен быть 152 байта");

// Сама заготовка в образе. Ссылка на неё держит секцию от выброса сборщиком
// мусора (--gc-sections).
__attribute__((used, aligned(4)))
extern const Blob g_factory_cfg;

inline const Blob* blob() { return &g_factory_cfg; }

// CRC32 (IEEE, как zlib.crc32) — тот же алгоритм считает прошивальщик.
uint32_t crc32(const uint8_t* data, int len);

// false — блока нет, версия не наша, длина не та или CRC не сошлась.
bool isValid(const Blob* b, Payload& out);

}  // namespace DarktecFactoryCfg

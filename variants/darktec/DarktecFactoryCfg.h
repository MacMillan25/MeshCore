#pragma once

#include <stdint.h>

// Блок «заводских» настроек, который прошивальщик вписывает прямо в образ
// прошивки (UF2 или OTA-пакет). Прошивка читает его на старте и, если метка
// совпала и CRC сошлась, один раз переносит значения в prefs.
//
// Почему массив, а не отдельная секция линкера: массив лежит внутри образа,
// поэтому патчить его можно, просто заменив байты в UF2 — количество блоков и
// их адреса не меняются. Пришивать новый блок в конец UF2 пришлось бы с
// пересчётом numBlocks во всех блоках, а это лишний риск.
//
// Раскладка (little-endian):
//   0..15   метка "DARKTEC-FACTORY\x01" — по ней прошивальщик находит блок
//   16      версия раскладки (kCfgVersion)
//   17      длина полезной части в байтах
//   18..19  резерв (0)
//   20..23  CRC32 полезной части
//   24..    сама полезная часть (kCfgPayloadSize байт), пока нули
//
// Пока прошивка блок НЕ читает — это заготовка: сначала надо проверить, что
// патч образа ничего не ломает.
namespace DarktecFactoryCfg {

constexpr uint8_t kVersion = 1;
constexpr int kMarkerSize = 16;
constexpr int kHeaderSize = 24;
constexpr int kPayloadSize = 128;

// Магия: 15 байт текста + номер версии. В образе встречается один раз.
constexpr uint8_t kMarker[kMarkerSize] = {
    'D', 'A', 'R', 'K', 'T', 'E', 'C', '-', 'F', 'A', 'C', 'T', 'O', 'R', 'Y', 0x01
};

struct Blob {
  uint8_t marker[kMarkerSize];
  uint8_t version;
  uint8_t length;
  uint8_t reserved[2];
  uint32_t crc32;
  uint8_t payload[kPayloadSize];
};

// Сама заготовка. `used` — чтобы компилятор не выбросил её, `volatile const` —
// чтобы не подставил значения из инициализатора вместо чтения из флеша.
__attribute__((used, aligned(4)))
extern const Blob g_factory_cfg;

// Указатель для кода прошивки (позже здесь будет чтение настроек).
const Blob* blob();

}  // namespace DarktecFactoryCfg

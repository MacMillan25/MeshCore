#include "DarktecFactoryCfg.h"

#include <string.h>

namespace DarktecFactoryCfg {

// Заготовка блока заводских настроек: все нули. Прошивальщик находит её по
// метке и вписывает значения вместе с CRC32.
__attribute__((used, retain, aligned(4), section(".rodata.factory_cfg")))
const Blob g_factory_cfg = {
    { 'D', 'A', 'R', 'K', 'T', 'E', 'C', '-', 'F', 'A', 'C', 'T', 'O', 'R', 'Y', 0x01 },
    kVersion,
    0,                 // длина полезной части: 0 = блок пустой
    { 0, 0 },
    0,                 // CRC32
    { 0 }              // payload
};

uint32_t crc32(const uint8_t* data, int len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (int i = 0; i < len; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
  }
  return ~crc;
}

bool isValid(const Blob* b, Payload& out) {
  if (b == nullptr) return false;
  if (memcmp(b->marker, kMarker, kMarkerSize) != 0) return false;
  if (b->version != kVersion) return false;
  if (b->length != kPayloadSize) return false;   // 0 = заготовка, ничего не применять
  if (crc32(b->payload, kPayloadSize) != b->crc32) return false;
  memcpy(&out, b->payload, kPayloadSize);
  return true;
}

}  // namespace DarktecFactoryCfg

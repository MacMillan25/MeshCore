#include "DarktecFactoryCfg.h"

namespace DarktecFactoryCfg {

// Заготовка блока заводских настроек. Прошивальщик находит её по метке и
// вписывает значения; сама прошивка пока только держит место — чтение
// появится отдельным шагом, после проверки, что патч образа безопасен.
extern const Blob g_factory_cfg;

__attribute__((used, retain, aligned(4), section(".rodata.factory_cfg")))
const Blob g_factory_cfg = {
    { 'D', 'A', 'R', 'K', 'T', 'E', 'C', '-', 'F', 'A', 'C', 'T', 'O', 'R', 'Y', 0x01 },
    kVersion,
    0,                 // длина полезной части: 0 = блок пустой
    { 0, 0 },
    0,                 // CRC32
    { 0 }              // payload
};

const Blob* blob() { return &g_factory_cfg; }

}  // namespace DarktecFactoryCfg

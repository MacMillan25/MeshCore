#include <Arduino.h>
#include <SPI.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

DarktecBoard board;

// Скорость SPI к модулю. Провода на макетке длинные, поэтому её можно
// понизить флагом сборки: -D LORA_SPI_HZ=1000000.
#ifndef LORA_SPI_HZ
#define LORA_SPI_HZ 2000000
#endif

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI,
                               SPISettings(LORA_SPI_HZ, MSBFIRST, SPI_MODE0));


WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
  DarktecSensorManager sensors(nmea);
#else
  DarktecSensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);
#endif

static bool g_radio_ok = false;

// ---- RF-тракт модуля NiceRF LoRa2021F33-2G4 ----------------------------------
// Модуль сам тракт не коммутирует: по блок-схеме его усилители, LNA и ключ
// 5-вольтового питания висят на ногах DIO5..DIO8 чипа:
//   DIO7 -> Power_EN (PMOS, питание усилителя 5 В)
//   DIO8 -> TXEN усилителя 2.4 ГГц
//   DIO6 -> TXEN усилителя sub-GHz (и ключ антенны),
//   DIO5 -> LNA 2.4 ГГц (по даташиту модуля его нормально держать высоким).
// Таблица RF-ключа говорит чипу, в каких режимах какая нога высокая; дальше он
// дёргает их сам при переходе в Rx/Tx, без участия прошивки.
// Без этой таблицы ноги висят в Hi-Z, усилитель не включается — приём еле
// теплится, передачи нет вообще.

static const uint32_t rf_dios[5] = {
  RADIOLIB_LR2021_DIO5, RADIOLIB_LR2021_DIO6, RADIOLIB_LR2021_DIO7,
  RADIOLIB_LR2021_DIO8, RADIOLIB_NC
};

// Основной вариант: питание усилителя только на передаче, 2.4G LNA — только
// в высокочастотных режимах.
static const Module::RfSwitchMode_t rf_table_main[] = {
  { LR2021::MODE_STBY,  { LOW,  LOW,  LOW,  LOW,  LOW } },
  { LR2021::MODE_RX,    { LOW,  LOW,  LOW,  LOW,  LOW } },
  { LR2021::MODE_TX,    { LOW,  HIGH, HIGH, LOW,  LOW } },
  { LR2021::MODE_RX_HF, { HIGH, LOW,  HIGH, LOW,  LOW } },
  { LR2021::MODE_TX_HF, { HIGH, LOW,  HIGH, HIGH, LOW } },
  END_OF_MODE_TABLE,
};

// Вариант, если питание усилителя держат постоянно.
static const Module::RfSwitchMode_t rf_table_pwr_on[] = {
  { LR2021::MODE_STBY,  { LOW,  LOW,  HIGH, LOW,  LOW } },
  { LR2021::MODE_RX,    { LOW,  LOW,  HIGH, LOW,  LOW } },
  { LR2021::MODE_TX,    { LOW,  HIGH, HIGH, LOW,  LOW } },
  { LR2021::MODE_RX_HF, { HIGH, LOW,  HIGH, LOW,  LOW } },
  { LR2021::MODE_TX_HF, { HIGH, LOW,  HIGH, HIGH, LOW } },
  END_OF_MODE_TABLE,
};

// Все ноги в нуле — «ничего не управляем».
static const Module::RfSwitchMode_t rf_table_off[] = {
  { LR2021::MODE_STBY,  { LOW, LOW, LOW, LOW, LOW } },
  { LR2021::MODE_RX,    { LOW, LOW, LOW, LOW, LOW } },
  { LR2021::MODE_TX,    { LOW, LOW, LOW, LOW, LOW } },
  { LR2021::MODE_RX_HF, { LOW, LOW, LOW, LOW, LOW } },
  { LR2021::MODE_TX_HF, { LOW, LOW, LOW, LOW, LOW } },
  END_OF_MODE_TABLE,
};

// На передаче поднимаем оба TXEN: если перепутано, какой из DIO6/DIO8 тянет
// усилитель 868 МГц, тракт всё равно откроется.
static const Module::RfSwitchMode_t rf_table_both_txen[] = {
  { LR2021::MODE_STBY,  { LOW,  LOW,  LOW,  LOW,  LOW } },
  { LR2021::MODE_RX,    { LOW,  LOW,  LOW,  LOW,  LOW } },
  { LR2021::MODE_TX,    { LOW,  HIGH, HIGH, HIGH, LOW } },
  { LR2021::MODE_RX_HF, { HIGH, LOW,  HIGH, LOW,  LOW } },
  { LR2021::MODE_TX_HF, { HIGH, LOW,  HIGH, HIGH, LOW } },
  END_OF_MODE_TABLE,
};

// Питание усилителя постоянно, на передаче — все TXEN.
static const Module::RfSwitchMode_t rf_table_max[] = {
  { LR2021::MODE_STBY,  { LOW,  LOW,  HIGH, LOW,  LOW } },
  { LR2021::MODE_RX,    { LOW,  LOW,  HIGH, LOW,  LOW } },
  { LR2021::MODE_TX,    { LOW,  HIGH, HIGH, HIGH, LOW } },
  { LR2021::MODE_RX_HF, { HIGH, LOW,  HIGH, LOW,  LOW } },
  { LR2021::MODE_TX_HF, { HIGH, LOW,  HIGH, HIGH, LOW } },
  END_OF_MODE_TABLE,
};

static uint8_t g_rf_preset = 1;

// Маска состояния DIO5..DIO8 в режиме приёма: бит0 = DIO5, бит1 = DIO6,
// бит2 = DIO7, бит3 = DIO8. Передача при этом всегда остаётся рабочей
// (DIO6 + DIO7 высокие) — её мы уже проверили на живом эфире.
// Нужна, чтобы перебрать варианты приёма без перепрошивки.
static uint8_t g_rx_mask = 0;

static uint32_t rx_bit(uint8_t mask, uint8_t i) {
  return ((mask >> i) & 1) ? HIGH : LOW;
}

static void build_custom_table(uint8_t mask, Module::RfSwitchMode_t* t) {
  t[0] = { LR2021::MODE_STBY,  { rx_bit(mask, 0), rx_bit(mask, 1), rx_bit(mask, 2), rx_bit(mask, 3), LOW } };
  t[1] = { LR2021::MODE_RX,    { rx_bit(mask, 0), rx_bit(mask, 1), rx_bit(mask, 2), rx_bit(mask, 3), LOW } };
  t[2] = { LR2021::MODE_TX,    { LOW, HIGH, HIGH, LOW, LOW } };
  t[3] = { LR2021::MODE_RX_HF, { HIGH, LOW, HIGH, LOW, LOW } };
  t[4] = { LR2021::MODE_TX_HF, { HIGH, LOW, HIGH, HIGH, LOW } };
  t[5] = END_OF_MODE_TABLE;
}

static Module::RfSwitchMode_t g_custom_table[6];

void radio_set_rx_mask(uint8_t mask) {
  g_rx_mask = mask & 0x0F;
  build_custom_table(g_rx_mask, g_custom_table);
  radio.setRfSwitchTable(rf_dios, g_custom_table);
  g_rf_preset = 0;
  radio.standby();
  radio.startReceive();
}

uint8_t radio_rx_mask() { return g_rx_mask; }

void radio_apply_rf(uint8_t preset) {
  const Module::RfSwitchMode_t* t = rf_table_main;
  switch (preset) {
    case 2: t = rf_table_pwr_on; break;
    case 3: t = rf_table_off; break;
    case 4: t = rf_table_both_txen; break;
    case 5: t = rf_table_max; break;
    default: preset = 1; break;
  }

  radio.setRfSwitchTable(rf_dios, t);
  g_rf_preset = preset;

  radio.standby();
  radio.startReceive();
}

uint8_t radio_rf_preset() { return g_rf_preset; }

// Ручное управление ногой DIO — для разборки с трактом на живом железе.
void radio_dio(uint8_t dio, bool high) {
  if (dio < 5 || dio > 11) return;
  radio.dioLevel(dio, high);
}

// Смена ноги прерывания: старую гасим, новую переводим в IRQ и заново
// запускаем приём.
void radio_set_irq(uint8_t dio) {
  if (dio < 5 || dio > 11) return;
  const uint8_t old = (uint8_t)radio.irqDioNum;
  if (old >= 5 && old <= 11 && old != dio) {
    radio.dioNone(old);
  }
  radio.irqDioNum = dio;
  radio.dioFunction(dio, RADIOLIB_LR2021_DIO_FUNCTION_IRQ,
                    dio == 5 ? RADIOLIB_LR2021_DIO_SLEEP_PULL_UP
                             : RADIOLIB_LR2021_DIO_SLEEP_PULL_AUTO);
  radio.standby();
  radio.startReceive();
}

uint8_t radio_irq_dio() { return (uint8_t)radio.irqDioNum; }

// Вернуть DIO5..DIO8 в Hi-Z: так модуль вёл себя до всех наших настроек,
// и на той прошивке один пакет от репитера поймали. Нужно для сравнения.
void radio_dio_release(void) {
  for (uint8_t d = 5; d <= 8; d++) {
    radio.dioNone(d);
  }
  radio.standby();
  radio.startReceive();
}

bool radio_init() {
  rtc_clock.begin(Wire);

  // LR2021 инициализируется целиком внутри std_init(): частота, полоса, SF,
  // CR, sync word, мощность, TCXO. Если модуль без TCXO, драйвер сам
  // повторяет инициализацию с нулевым напряжением TCXO.
  g_radio_ok = radio.std_init(&SPI);

  Serial.print("[LR2021] init ");
  Serial.println(g_radio_ok ? "ok" : "FAIL");

  if (g_radio_ok) {
    radio_apply_rf(1);

    float rssi = 0.0f;
    if (radio.getRssiInst(&rssi) == RADIOLIB_ERR_NONE) {
      Serial.print("[LR2021] rssi ");
      Serial.println(rssi, 1);
    }
  }
  return g_radio_ok;
}

// Строка для CLI `lr2021`: что видит драйвер на самом деле. Нужна, когда
// «на экране тишина»: видно статус прерываний чипа, RSSI и счётчики пакетов.
// Форматируем без %f: в newlib nano float-printf может быть выключен.
void radio_diag(char* reply, size_t len) {
  float rssi = 0.0f;
  const int rssi_err = radio.getRssiInst(&rssi);
  const uint32_t irq = radio.getIrqFlags();
  const uint32_t f_khz = (uint32_t)(radio_driver.getParamFreq() * 1000.0f + 0.5f);
  const uint32_t bw10 = (uint32_t)(radio_driver.getParamBw() * 10.0f + 0.5f);

  snprintf(reply, len,
           "LR2021 %s f=%lu.%03lu bw=%lu.%lu sf=%u cr=%u rf=%u rxm=%u dio=%u "
           "irq=0x%06lX rssi=%d(%d) rx=%lu s=%lu e=%lu nf=%d p=%lu d=%lu m%d",
           g_radio_ok ? "ok" : "FAIL",
           (unsigned long)(f_khz / 1000), (unsigned long)(f_khz % 1000),
           (unsigned long)(bw10 / 10), (unsigned long)(bw10 % 10),
           (unsigned)radio_driver.getParamSf(), (unsigned)radio_driver.getParamCr(),
           (unsigned)radio_rf_preset(), (unsigned)radio_rx_mask(),
           (unsigned)radio_irq_dio(),
           (unsigned long)irq, (int)(rssi < 0 ? rssi - 0.5f : rssi + 0.5f), rssi_err,
           (unsigned long)radio_driver.getPacketsRecv(),
           (unsigned long)radio_driver.getPacketsSent(),
           (unsigned long)radio_driver.getPacketsRecvErrors(),
           radio_driver.getNoiseFloor(),
           (unsigned long)radio_driver.getStatPoll(),
           (unsigned long)radio_driver.getStatRead(),
           (int)radio_driver.isInRecvMode());
}

// `lr2021 scan` — слушаем эфир N секунд и считаем, что именно чип видит:
// преамбулы, валидные заголовки, принятые пакеты, ошибки CRC заголовка и
// полезной нагрузки. По этой картинке видно, где рвётся: тракт (ничего нет),
// настройки (преамбулы без заголовков) или частота/модуляция (пакеты, но CRC).
void radio_scan(uint8_t seconds, char* reply, size_t len) {
  if (seconds < 1) seconds = 1;
  if (seconds > 30) seconds = 30;

  radio.standby();
  radio.clearIrqFlags(0xFFFFFFFF);
  const int16_t st = radio.startReceive();

  uint32_t pre = 0, hdr = 0, done = 0, crc = 0, hdrerr = 0, to = 0;
  int16_t rssi_min = 0, rssi_max = 0;
  bool first = true;
  const uint32_t end = millis() + (uint32_t)seconds * 1000UL;
  uint32_t next_rssi = 0;

  while ((int32_t)(millis() - end) < 0) {
    const uint32_t irq = radio.getIrqFlags();
    if (irq) {
      if (irq & RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED) pre++;
      if (irq & RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID) hdr++;
      if (irq & RADIOLIB_LR2021_IRQ_CRC_ERROR) crc++;
      if (irq & RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR) hdrerr++;
      if (irq & RADIOLIB_LR2021_IRQ_RX_DONE) done++;
      if (irq & RADIOLIB_LR2021_IRQ_TIMEOUT) to++;
      radio.clearIrqFlags(irq);
    }
    if ((int32_t)(millis() - next_rssi) >= 0) {
      next_rssi = millis() + 50;
      float r = 0.0f;
      if (radio.getRssiInst(&r) == RADIOLIB_ERR_NONE) {
        const int16_t v = (int16_t)(r < 0 ? r - 0.5f : r + 0.5f);
        if (first || v < rssi_min) rssi_min = v;
        if (first || v > rssi_max) rssi_max = v;
        first = false;
      }
    }
    delay(1);
  }

  radio.clearIrqFlags(0xFFFFFFFF);
  radio.standby();
  const int16_t st2 = radio.startReceive();
  uint8_t ptype = 0xFF;
  radio.getPacketType(&ptype);

  snprintf(reply, len,
           "scan %us st=%d/%d pt=%u pre=%lu hdr=%lu done=%lu crc=%lu hdrerr=%lu "
           "to=%lu rssi=%d..%d",
           (unsigned)seconds, (int)st, (int)st2, (unsigned)ptype,
           (unsigned long)pre, (unsigned long)hdr, (unsigned long)done,
           (unsigned long)crc, (unsigned long)hdrerr, (unsigned long)to,
           (int)rssi_min, (int)rssi_max);
}

// `lr2021 spi` — проверка линии SPI: 200 раз читаем версию чипа (она
// неизменна) и считаем, сколько ответов не совпало с первым. Если есть
// расхождения или ответы FF/FF — дело в проводке, земле или скорости шины,
// а не в радио.
void radio_spi_test(char* reply, size_t len) {
  uint8_t ref_maj = 0, ref_min = 0;
  (void)radio.getVersion(&ref_maj, &ref_min);

  int bad = 0, all_ff = 0, errs = 0;
  for (int i = 0; i < 200; i++) {
    uint8_t maj = 0, min = 0;
    const int16_t st = radio.getVersion(&maj, &min);
    if (st != RADIOLIB_ERR_NONE) {
      errs++;
      continue;
    }
    if (maj != ref_maj || min != ref_min) {
      bad++;
      if (maj == 0xFF && min == 0xFF) all_ff++;
    }
  }

  snprintf(reply, len, "spi v=%u.%u n=200 bad=%d ff=%d err=%d hz=%d",
           (unsigned)ref_maj, (unsigned)ref_min, bad, all_ff, errs, (int)LORA_SPI_HZ);
}

// `lr2021 tx [n]` — передать n сырых пакетов в обход меша и посмотреть,
// доходит ли чип до TX_DONE. Если TX_DONE есть, а никто не слышит — виноват
// тракт (антенна, порт, PA), а не прошивка.
void radio_tx_test(uint8_t count, char* reply, size_t len) {
  if (count < 1) count = 1;
  if (count > 10) count = 10;

  static const uint8_t pkt[10] = { 'L', 'R', '2', '0', '2', '1', 'T', 'E', 'S', 'T' };

  uint8_t ok = 0, fail = 0, noirq = 0;
  int16_t last_err = 0;

  for (uint8_t i = 0; i < count; i++) {
    radio.standby();
    radio.clearIrqFlags(0xFFFFFFFF);
    last_err = radio.startTransmit((uint8_t*)pkt, sizeof(pkt));
    if (last_err != RADIOLIB_ERR_NONE) {
      fail++;
      continue;
    }

    const uint32_t end = millis() + 2000;
    bool done = false, timeout = false;
    while ((int32_t)(millis() - end) < 0) {
      const uint32_t irq = radio.getIrqFlags();
      if (irq & RADIOLIB_LR2021_IRQ_TX_DONE) { done = true; break; }
      if (irq & RADIOLIB_LR2021_IRQ_TIMEOUT) { timeout = true; break; }
      delay(1);
    }
    if (done) {
      ok++;
    } else {
      fail++;
      if (!timeout) noirq++;
    }
    radio.finishTransmit();
    delay(60);
  }

  radio.standby();
  radio.startReceive();

  snprintf(reply, len, "tx n=%u ok=%u fail=%u noirq=%u e=%d",
           (unsigned)count, (unsigned)ok, (unsigned)fail, (unsigned)noirq,
           (int)last_err);
}

// `lr2021 sweep [сек]` — перебор всех 16 состояний DIO5..DIO8 в приёме.
// Для каждой маски слушаем эфир и считаем преамбулы/принятые пакеты. Передача
// при этом остаётся рабочей, поэтому в эфире ничего не ломается.
void radio_sweep(uint8_t secs, char* reply, size_t len) {
  if (secs < 1) secs = 1;
  if (secs > 5) secs = 5;

  const uint8_t orig = g_rx_mask;
  char out[200];
  size_t pos = 0;
  pos += snprintf(out + pos, sizeof(out) - pos, "sweep:");

  for (uint8_t m = 0; m < 16 && pos < sizeof(out) - 16; m++) {
    radio_set_rx_mask(m);
    uint32_t pre = 0, done = 0, crc = 0;
    const uint32_t end = millis() + (uint32_t)secs * 1000UL;
    while ((int32_t)(millis() - end) < 0) {
      const uint32_t irq = radio.getIrqFlags();
      if (irq) {
        if (irq & RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED) pre++;
        if (irq & RADIOLIB_LR2021_IRQ_RX_DONE) done++;
        if (irq & RADIOLIB_LR2021_IRQ_CRC_ERROR) crc++;
        radio.clearIrqFlags(irq);
      }
      delay(1);
    }
    pos += snprintf(out + pos, sizeof(out) - pos, " %u:%lu/%lu",
                    (unsigned)m, (unsigned long)pre, (unsigned long)done);
    Serial.print("[sweep] mask ");
    Serial.print(m);
    Serial.print(" pre=");
    Serial.print(pre);
    Serial.print(" done=");
    Serial.print(done);
    Serial.print(" crc=");
    Serial.println(crc);
  }

  radio_set_rx_mask(orig);
  snprintf(reply, len, "%s", out);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // новая случайная идентичность
}

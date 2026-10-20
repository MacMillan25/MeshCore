#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

DarktecBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

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

bool radio_init() {
  rtc_clock.begin(Wire);

  // LR2021 инициализируется целиком внутри std_init(): частота, полоса, SF,
  // CR, sync word, мощность, TCXO. Если модуль без TCXO, драйвер сам
  // повторяет инициализацию с нулевым напряжением TCXO.
  g_radio_ok = radio.std_init(&SPI);

  Serial.print("[LR2021] init ");
  Serial.println(g_radio_ok ? "ok" : "FAIL");

  if (g_radio_ok) {
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

  snprintf(reply, len,
           "LR2021 %s f=%lu.%03lu bw=%d sf=%u cr=%u txp=%d irq=0x%06lX rssi=%d e%d "
           "rx=%lu sent=%lu err=%lu nf=%d",
           g_radio_ok ? "ok" : "FAIL",
           (unsigned long)(f_khz / 1000), (unsigned long)(f_khz % 1000),
           (int)(radio_driver.getParamBw() + 0.5f),
           (unsigned)radio_driver.getParamSf(), (unsigned)radio_driver.getParamCr(),
           (int)board.getTxPowerDbm(),
           (unsigned long)irq, (int)(rssi < 0 ? rssi - 0.5f : rssi + 0.5f), rssi_err,
           (unsigned long)radio_driver.getPacketsRecv(),
           (unsigned long)radio_driver.getPacketsSent(),
           (unsigned long)radio_driver.getPacketsRecvErrors(),
           radio_driver.getNoiseFloor());
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
  radio.startReceive();

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
  radio.startReceive();

  snprintf(reply, len,
           "scan %us pre=%lu hdr=%lu done=%lu crc=%lu hdrerr=%lu to=%lu rssi=%d..%d",
           (unsigned)seconds, (unsigned long)pre, (unsigned long)hdr,
           (unsigned long)done, (unsigned long)crc, (unsigned long)hdrerr,
           (unsigned long)to, (int)rssi_min, (int)rssi_max);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // новая случайная идентичность
}

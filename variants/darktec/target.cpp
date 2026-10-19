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
  const uint32_t f_khz = (uint32_t)(radio.getFreqMHz() * 1000.0f + 0.5f);

  snprintf(reply, len,
           "LR2021 %s f=%lu.%03lu sf=%u txp=%d irq=0x%06lX rssi=%d e%d rx=%lu sent=%lu err=%lu nf=%d",
           g_radio_ok ? "ok" : "FAIL",
           (unsigned long)(f_khz / 1000), (unsigned long)(f_khz % 1000),
           (unsigned)radio.getSpreadingFactor(), (int)board.getTxPowerDbm(),
           (unsigned long)irq, (int)(rssi < 0 ? rssi - 0.5f : rssi + 0.5f), rssi_err,
           (unsigned long)radio_driver.getPacketsRecv(),
           (unsigned long)radio_driver.getPacketsSent(),
           (unsigned long)radio_driver.getPacketsRecvErrors(),
           radio_driver.getNoiseFloor());
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // новая случайная идентичность
}

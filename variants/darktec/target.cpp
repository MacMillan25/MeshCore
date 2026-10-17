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

bool radio_init() {
  rtc_clock.begin(Wire);

  if (!radio.std_init(&SPI)) {
    return false;
  }

  // До загрузки prefs радио должно работать хоть как-то, поэтому сначала
  // ставим модуль по умолчанию. Сохранённый в настройках применяется позже,
  // из darktec_apply_saved_radio_module().
  board.setRadioModule(DARKTEC_RADIO_DEFAULT_MODULE);
  return true;
}

// Пин 6 модуля (P0.17) у разных модулей означает разное:
//   E22-900M33S / M30S / HT-RA62 — RXEN, им управляет RadioLib: на передаче 0,
//                                  в приёме 1;
//   E22P-868M30S               — EN, общий включатель тракта. RadioLib его не
//                                  должен трогать: на передаче он опустил бы
//                                  ногу в 0 и выключил усилитель.
// Поэтому для E22P RF-ключ отключается совсем, а нога держится высоким.
void darktec_radio_set_rf(uint8_t module_idx) {
  if (module_idx == DARKTEC_MODULE_E22P) {
    radio.setRfSwitchPins(RADIOLIB_NC, RADIOLIB_NC);
  } else {
    radio.setRfSwitchPins(DARKTEC_RF_SWITCH_PIN, RADIOLIB_NC);
  }
  pinMode(DARKTEC_RF_SWITCH_PIN, OUTPUT);
  digitalWrite(DARKTEC_RF_SWITCH_PIN, HIGH);
}

void darktec_apply_saved_radio_module(uint8_t module_idx) {
  board.setRadioModule(module_idx);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // новая случайная идентичность
}

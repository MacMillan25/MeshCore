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

  // LR2021 инициализируется целиком внутри std_init(): частота, полоса, SF,
  // CR, sync word, мощность, TCXO. Если модуль без TCXO, драйвер сам
  // повторяет инициализацию с нулевым напряжением TCXO.
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // новая случайная идентичность
}

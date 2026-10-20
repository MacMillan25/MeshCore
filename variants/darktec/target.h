#pragma once
// Строка TZ для страницы часов на дисплее (POSIX).
// Примеры: "UTC0"           UTC
//          "MSK-3"          Москва (UTC+3)
//          "EET-2"          Восточная Европа (UTC+2)
//          "CET-1CEST,M3.5.0,M10.5.0/3"  Центральная Европа с DST
// Переопределение в platformio.ini: -DDISPLAY_TZ="MSK-3"
// См.: https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
#ifndef DISPLAY_TZ
#  define DISPLAY_TZ  "UTC0"
#endif


#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <DarktecBoard.h>
#include <DarktecLR2021Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#ifdef DISPLAY_CLASS
  #include <DarktecDisplay.h>
  #include <helpers/ui/SSD1306Display.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

#include <helpers/sensors/EnvironmentSensorManager.h>
#include <DarktecSensorManager.h>

extern DarktecBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
// Обёртка над EnvironmentSensorManager: добавляет удалённо управляемые выходы
// (RELAY = T2). Общий код для всех ролей — см. DarktecSensorManager.h.
extern DarktecSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();

// Диагностика для CLI `lr2021`: состояние чипа, статус прерываний, RSSI,
// счётчики. Пишет в reply не больше len байт.
void radio_diag(char* reply, size_t len);

// `lr2021 scan` — слушает эфир seconds секунд и считает преамбулы, заголовки,
// пакеты и ошибки CRC. Пишет итог в reply.
void radio_scan(uint8_t seconds, char* reply, size_t len);

// `lr2021 spi` — 200 чтений версии чипа: проверка целостности линии SPI.
void radio_spi_test(char* reply, size_t len);

// `lr2021 tx [n]` — передать n сырых пакетов и посчитать, доходит ли TX_DONE.
void radio_tx_test(uint8_t count, char* reply, size_t len);

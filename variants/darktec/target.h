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
#include <DarktecLR2021Radio.h>
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

// RF-тракт модуля: таблица RF-ключа (1 = основной вариант, 2 = питание
// усилителя постоянно, 3 = все DIO в нуле), ручное управление ногой и смена
// ноги прерывания. Всё это нужно на время разборки с железом.
void radio_apply_rf(uint8_t preset);
uint8_t radio_rf_preset();
void radio_dio(uint8_t dio, bool high);
void radio_set_irq(uint8_t dio);
uint8_t radio_irq_dio();

// Отпустить DIO5..DIO8 в Hi-Z (как было до настройки тракта).
void radio_dio_release(void);

// Состояние DIO5..DIO8 в приёме задаётся маской (бит0=DIO5 ... бит3=DIO8).
// Передача при этом не меняется — остаётся рабочей.
void radio_set_rx_mask(uint8_t mask);
uint8_t radio_rx_mask();

// Перебор всех 16 масок приёма: по секунд на вариант, считает преамбулы и
// принятые пакеты.
void radio_sweep(uint8_t secs, char* reply, size_t len);

// Приём в обход меша: печатает в порт каждый пакет с кодом ошибки чтения.
void radio_listen(uint8_t secs, char* reply, size_t len);

// Усиление приёмника (boosted gain) — включить/выключить на ходу.
void radio_set_boost(bool en, char* reply, size_t len);

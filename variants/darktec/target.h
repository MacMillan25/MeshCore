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
#include <DarktecSX1262Wrapper.h>
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

// Применение модуля к RF-тракту: 0=33S, 1=30S, 2=RA62 (pin 6 = RXEN, ведёт
// RadioLib), 3=E22P (pin 6 = EN, держится высоким вручную). Определена в
// target.cpp, где живёт объект радио; board зовёт её из setRadioModule().
void darktec_radio_set_rf(uint8_t module_idx);

// Вызывается после загрузки prefs: ставит модуль, выбранный в настройках.
// До этого работает DARKTEC_RADIO_DEFAULT_MODULE.
void darktec_apply_saved_radio_module(uint8_t module_idx);

/*
 * DarktecSensorManager.h — Darktec: удалённо управляемые выходы
 *
 * Обёртка над EnvironmentSensorManager. Ничего из базового класса не трогаем:
 * все вызовы, которые нас не касаются, уходят в базу как есть. Добавляем свои
 * ключи настроек, а дальше работает готовый механизм MeshCore — он один для
 * ВСЕХ ролей и обоих модулей:
 *
 *   ключ доступен как настройка сенсора  ->  SensorManager::setSettingValue()
 *
 * Оттуда его достают уже существующие пути, без единой правки протокола:
 *   - репитер        : личка от админа  -> CLI "sensor set ..."
 *   - комнатный серв.: личка от админа  -> CLI "sensor set ..."
 *   - сенсор         : личка от админа  -> CLI "sensor set ..."
 *   - компаньон      : Remote CLI (личка + PIN) ИЛИ канал TerminalCLI  -> CLI
 *   - компаньон от приложения: serial/BLE CMD_SET_CUSTOM_VAR "KEY:VALUE"
 *
 * Ключи (регистр не важен):
 *   RELAY ON | OFF | TOGGLE
 *   GPSPWR ON | OFF
 *
 * Ключ RELAY управляет транзистором T2 (нога MCU P0.08, Arduino-пин 0).
 * По требованию владельца реле при старте ВСЕГДА выключено.
 */

#pragma once

#include <Arduino.h>
#include <cstring>
#include <cctype>

#include <helpers/sensors/EnvironmentSensorManager.h>

// T2 = P0.08. В Arduino-нумерации DarkTec это пин 0 (см. g_ADigitalPinMap
// в variant.cpp). Переопределяется в platformio.ini как DARKTEC_RELAY_PIN.
#ifndef DARKTEC_RELAY_PIN
#define DARKTEC_RELAY_PIN 0
#endif

class DarktecSensorManager : public EnvironmentSensorManager {
  bool _relay = false;    // всегда стартуем с выключенного реле

  static bool _valueOn(const char* value) {
    return strcasecmp(value, "on") == 0
        || strcasecmp(value, "1") == 0
        || strcasecmp(value, "true") == 0
        || strcasecmp(value, "yes") == 0;
  }

  static bool _valueOff(const char* value) {
    return strcasecmp(value, "off") == 0
        || strcasecmp(value, "0") == 0
        || strcasecmp(value, "false") == 0
        || strcasecmp(value, "no") == 0;
  }

  static bool _wantToggle(const char* value) {
    return strcasecmp(value, "toggle") == 0
        || strcasecmp(value, "flip") == 0
        || strcasecmp(value, "switch") == 0;
  }

  bool _relayKey(const char* name) const {
    return strcasecmp(name, "relay") == 0 || strcasecmp(name, "t2") == 0;
  }

  bool _gpsPwrKey(const char* name) const {
    return strcasecmp(name, "gpspwr") == 0
        || strcasecmp(name, "gps_power") == 0
        || strcasecmp(name, "t1") == 0;
  }

public:
#if ENV_INCLUDE_GPS
  explicit DarktecSensorManager(LocationProvider& location)
      : EnvironmentSensorManager(location) {}
#else
  DarktecSensorManager() {}
#endif

  bool begin() override {
    pinMode(DARKTEC_RELAY_PIN, OUTPUT);
    digitalWrite(DARKTEC_RELAY_PIN, _relay ? HIGH : LOW);
    return EnvironmentSensorManager::begin();
  }

  void loop() override {
    EnvironmentSensorManager::loop();
    // При необходимости здесь появится отложенное включение GPS (T1) —
    // чипу надо дать время на старт после подачи питания.
  }

  // --- настройки: наши ключи идут первыми, затем как есть базовые ---------

  int getNumSettings() const override {
    return 1 + EnvironmentSensorManager::getNumSettings();
  }

  const char* getSettingName(int i) const override {
    if (i == 0) return "RELAY";
    return EnvironmentSensorManager::getSettingName(i - 1);
  }

  const char* getSettingValue(int i) const override {
    if (i == 0) return _relay ? "ON" : "OFF";
    return EnvironmentSensorManager::getSettingValue(i - 1);
  }

  bool setSettingValue(const char* name, const char* value) override {
    if (_relayKey(name)) {
      if (_valueOn(value)) {
        _relay = true;
      } else if (_valueOff(value)) {
        _relay = false;
      } else if (_wantToggle(value)) {
        _relay = !_relay;
      } else {
        return false;
      }
      digitalWrite(DARKTEC_RELAY_PIN, _relay ? HIGH : LOW);
      return true;
    }

    // Питание GPS (T1, P0.24) пока не реализовано: чипу нужна пауза на старт
    // после подачи питания, а драйвер GPS об этом не знает. Отвечаем понятной
    // ошибкой вместо тихой поломки детекта GPS.
    if (_gpsPwrKey(name)) {
      return false;
    }

    return EnvironmentSensorManager::setSettingValue(name, value);
  }
};

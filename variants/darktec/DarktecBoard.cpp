#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <nrf_gpio.h>

#include "DarktecBoard.h"

// Pack voltage window used only to seed the discharge counter after a cold boot.
// Normally supplied by battery_chemistry.h; the fallbacks keep this file
// buildable on its own.
#ifndef BATT_MAX_MILLIVOLTS
#define BATT_MAX_MILLIVOLTS 4200
#endif
#ifndef BATT_MIN_MILLIVOLTS
#define BATT_MIN_MILLIVOLTS 3000
#endif

// Coarse state of charge from pack voltage. The Li-ion discharge curve has a
// long plateau, so this is an anchor at best and is never used to track charge
// continuously — that is what the coulomb counter is for.
static float socFromVoltage(uint16_t mv) {
  if (mv >= (uint16_t)BATT_MAX_MILLIVOLTS) return 1.0f;
  if (mv <= (uint16_t)BATT_MIN_MILLIVOLTS) return 0.0f;
  return (float)(mv - (uint16_t)BATT_MIN_MILLIVOLTS)
       / (float)((uint16_t)BATT_MAX_MILLIVOLTS - (uint16_t)BATT_MIN_MILLIVOLTS);
}

#ifdef NRF52_POWER_MANAGEMENT
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel      = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock   = PWRMGT_VOLTAGE_BOOTLOCK,
  .lpcomp_low_refsel  = PWRMGT_LPCOMP_LOW_REFSEL,
};

void DarktecBoard::initiateShutdown(uint8_t reason) {
#if DARKTEC_BATT_PROTECT == DARKTEC_BATT_PROTECT_ADC
  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
      reason == SHUTDOWN_REASON_BOOT_PROTECT) {
    darktec::waitForBatteryRecovery(*this, SX126X_POWER_EN);
    return;  // недостижимо
  }
#endif

  digitalWrite(SX126X_POWER_EN, LOW);
  enterSystemOff(reason);
}
#endif

void DarktecBoard::powerOff() {
  // Hibernate / CLI — SYSTEMOFF, как у Rogovogor.
  // ADC-wait только для защиты батареи (initiateShutdown LOW_VOLTAGE/BOOT_PROTECT).
#ifdef PIN_USER_BTN
  while (digitalRead(PIN_USER_BTN) == LOW) {
    delay(10);
  }
#endif

  shutdownPeripherals();
  digitalWrite(SX126X_POWER_EN, LOW);

#ifdef PIN_USER_BTN
  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[PIN_USER_BTN], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#endif

#ifdef NRF52_POWER_MANAGEMENT
  enterSystemOff(SHUTDOWN_REASON_USER);
#else
  NRF52Board::powerOff();
#endif
}

void DarktecBoard::begin() {
    NRF52BoardDCDC::begin();
    btn_prev_state = HIGH;

    pinMode(PIN_VBAT_READ, INPUT);

    #ifdef BUTTON_PIN
      pinMode(BUTTON_PIN, INPUT_PULLUP);
    #endif

    #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
      Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
    #endif

    Wire.begin();

    pinMode(SX126X_POWER_EN, OUTPUT);
#ifdef NRF52_POWER_MANAGEMENT
#if DARKTEC_BATT_PROTECT == DARKTEC_BATT_PROTECT_ADC
    // Boot-lock по АЦП пакета (химия): ждём подъёма VBAT на делителе.
    if (darktec::batteryBelowThreshold(*this, PWRMGT_VOLTAGE_BOOTLOCK)) {
      darktec::waitForBatteryRecovery(*this, SX126X_POWER_EN);
    }
#endif
    // LPCOMP runtime alert не включаем (lpcomp_low_refsel=0).
    (void)power_config;
#endif
    digitalWrite(SX126X_POWER_EN, HIGH);
    delay(10);   // дать sx1262 время на включение

    // E22P: EN модуля — жёстко высокий, вне всяких библиотек. RadioLib этот пин
    // не знает (SX126X_RXEN/TXEN не заданы), поэтому уронить его на передаче
    // больше некому. Выставляем здесь, до radio_init(): begin() вызывается
    // раньше, чем радио вообще инициализируется.
    // У E22 пина EN нет: там нога 6 это RXEN, и ей управляет RadioLib.
#ifdef SX126X_EN_PIN
    pinMode(SX126X_EN_PIN, OUTPUT);
    digitalWrite(SX126X_EN_PIN, HIGH);
#endif

    readInaShuntReady();
}

// Opens the INA3221 on first use and makes sure every channel we read has the
// board's real shunt configured. The library otherwise falls back to 0.1R, and
// with the 0.05R parts on this board every amp reading would come out halved.
bool DarktecBoard::readInaShuntReady() {
  if (!_ina_ok) {
    _ina_ok = _ina.begin(TELEM_INA3221_ADDRESS, &Wire);
    if (!_ina_ok) {
      return false;
    }
  }
  _ina.setShuntResistance(DARKTEC_INA_CHARGE_CH, TELEM_INA3221_SHUNT_VALUE);
  _ina.setShuntResistance(DARKTEC_INA_BATT_CH, TELEM_INA3221_SHUNT_VALUE);
  return true;
}

// Периодический тик, раз в секунду, что бы ни делал UI. Задачи:
//
//   1. Кормить модель питания напряжением ПАКЕТА В ПОКОЕ. Под нагрузкой
//      клеммы просажены (1.5 А при передаче), а в полке кривой 3-5 мВ на 1%
//      SOC, поэтому замер под передачей уводит процент на единицы. Модель
//      сама решает, годится ли замер, по тишине в эфире.
//
//   2. Считать «залито за сессию» и состояние зарядника для страницы INA.
//      В SOC и ETA это НЕ участвует — процент там берётся из напряжения.
//
// Побочный эффект: раз в секунду читается напряжение батареи, поэтому
// защёлкнутое значение в телеметрии тоже посвежеет.
void DarktecBoard::loop() {
  uint32_t now = millis();
  if (_charge_last_ms != 0 && (now - _charge_last_ms) < DARKTEC_CHARGE_TICK_MS) {
    return;
  }
  uint32_t dt = (_charge_last_ms == 0) ? 0 : (now - _charge_last_ms);
  _charge_last_ms = now;

  if (!readInaShuntReady()) {
    return;
  }

  // Ток пакета нужен модели: по нему компенсируется падение на внутреннем
  // сопротивлении, иначе во время заряда клеммы дают ложные 100%.
  // Знак у INA3221: «+» разряд, «−» заряд.
  float batt_ma = 0.0f;
  {
    float a = _ina.getCurrentAmps(DARKTEC_INA_BATT_CH);
    if (!isnan(a)) batt_ma = a * 1000.0f;
  }

  // Напряжение, ток и тишина в эфире — в модель.
  _pm.update(getBattMilliVolts(), batt_ma, now, isLoRaActivity());
  _pm.noteWindowTick(DARKTEC_CHARGE_TICK_MS);

  float chrg_v = _ina.getBusVoltage(DARKTEC_INA_CHARGE_CH);
  if (isnan(chrg_v)) {
    return;
  }
  bool present = (chrg_v > DARKTEC_CHARGE_DETECT_V);

  if (present && !_charger_present) {
    // Сессия заряда начинается заново. Состояние заряда пакета при этом
    // не сбрасывается: оно берётся из напряжения и от зарядника не зависит.
    _charge_mah = 0.0f;
    _charge_session_ms = 0;
    _full_hold_ms = 0;
  }

  // Провал длиннее клампа означает, что суперцикл залип. Считаем клампнутый
  // отрезок с имеющимся показанием — это ближе к правде, чем потерять его.
  if (dt > DARKTEC_CHARGE_MAX_GAP_MS) {
    dt = DARKTEC_CHARGE_MAX_GAP_MS;
  }

  if (present) {
    if (dt > 0) {
      float amps = _ina.getCurrentAmps(DARKTEC_INA_BATT_CH);
      if (!isnan(amps) && amps > 0.0f) {
        // A * s / 3600 == mAh. Только счётчик сессии для страницы INA.
        _charge_mah += amps * (float)dt / 3600.0f;
        _charge_session_ms += dt;
      }
      // Долгий низкий ток при подключённом заряднике — банка взяла всё.
      if (!isnan(amps) && amps * 1000.0f < DARKTEC_CHARGE_FULL_MA) {
        _full_hold_ms += dt;
      } else {
        _full_hold_ms = 0;
      }
    }
  }

  _charger_present = present;
}

void DarktecBoard::pollChargeSense() {
  uint32_t now = millis();
  if (_ina_last_ms != 0 && (now - _ina_last_ms) < 250) {
    return;
  }
  _ina_last_ms = now;

  if (!readInaShuntReady()) {
    return;
  }

  float amps = _ina.getCurrentAmps(DARKTEC_INA_CHARGE_CH);
  if (isnan(amps)) {
    return;
  }
  float ma = amps * 1000.0f;
  if (_charging) {
    _charging = ma > DARKTEC_CHARGE_OFF_MA;
  } else {
    _charging = ma > DARKTEC_CHARGE_ON_MA;
  }
}

bool DarktecBoard::isCharging() {
  pollChargeSense();
  return _charging;
}

bool DarktecBoard::isExternalPowered() {
  // Зарядка пакета — INA3221 CH2. USB VBUS MCU к зарядке не относится,
  // но для KEEP_DISPLAY / CLI «external» учитываем и его (DFU, кабель).
  return isCharging() || NRF52Board::isExternalPowered();
}

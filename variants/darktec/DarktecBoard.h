#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

#define P_LORA_NSS 13 //P1.13 45
#define P_LORA_DIO_1 11 //P0.10 10
#define P_LORA_RESET 10 //P0.09 9
#define P_LORA_BUSY  16 //P0.29 29
#define P_LORA_MISO  15 //P0.02 2
#define P_LORA_SCLK  12 //P1.11 43
#define P_LORA_MOSI  14 //P1.15 47
#define SX126X_POWER_EN 21 //P0.13 13

/*
 * Управление RF-трактом модуля. Разводка платы (обе ревизии):
 *   пин 6 модуля -> P0.17 (Arduino 2)
 *   пин 7 модуля -> DIO2 модуля (пин 8), напрямую, без ноги MCU
 *
 * Направление передачи задаёт DIO2, и его трогать не нужно: чип SX1262 держит
 * его высоким при передаче и низким в остальное время (SX126X_DIO2_AS_RF_SWITCH).
 * На железе проверено — TX = 3.3 В, RX = 0 В, работает правильно на обоих модулях.
 *
 * Пин 6 у E22 и E22P означает РАЗНОЕ, поэтому он разведён флагом
 * DARKTEC_RADIO_E22P (задаётся в platformio.ini, секции Darktec_e22 / Darktec_e22p):
 *
 *   E22-900M30S  — пин 6 это RXEN. Прежняя логика: RadioLib ведёт ногу по
 *                  таблице RF-ключа, на передаче опускает в 0. Работает.
 *
 *   E22P-868M30S — пин 6 это EN, включатель тракта. Если отдать его RadioLib,
 *                  на передаче нога уходит в 0 и тракт выключается: приём
 *                  пропадает, слышно только в упор. Никакая раскладка таблицы
 *                  RF-ключа этого не лечит — проверено на железе. Поэтому у E22P
 *                  пин 6 полностью выведен из-под библиотеки (ни SX126X_RXEN,
 *                  ни SX126X_TXEN не заданы — RadioLib не трогает ни одной ноги)
 *                  и держится высоким вручную из DarktecBoard::begin().
 *                  Замерено после фикса: приём 3.3 В, передача 3.3 В, приём живой.
 */
#ifdef DARKTEC_RADIO_E22P
  /* E22P: EN всегда высокий, библиотеке не подчиняется. */
  #define SX126X_EN_PIN 2 // P0.17 — EN модуля
#else
  /* E22: пин 6 это RXEN, им управляет RadioLib по таблице RF-ключа. */
  #define SX126X_RXEN 2 // P0.17
  #define SX126X_TXEN RADIOLIB_NC
#endif

#define SX126X_DIO2_AS_RF_SWITCH  true
#define SX126X_DIO3_TCXO_VOLTAGE (1.8f)

#define  PIN_VBAT_READ 17
// Делитель батареи: 100к / 100к. Дефолт ADC_MULTIPLIER — в variant.h (1.750f).
#ifndef ADC_MULTIPLIER
#define  ADC_MULTIPLIER   (1.750f)
#endif

#include "DarktecAdcPower.h"
#include <Adafruit_INA3221.h>

// INA3221: зарядка пакета на 2-м канале (Adafruit 0-based: CH1=0, CH2=1, CH3=2).
#ifndef TELEM_INA3221_ADDRESS
#define TELEM_INA3221_ADDRESS 0x42
#endif
#ifndef TELEM_INA3221_SHUNT_VALUE
#define TELEM_INA3221_SHUNT_VALUE 0.050f
#endif
#ifndef DARKTEC_INA_CHARGE_CH
#define DARKTEC_INA_CHARGE_CH 1
#endif
#ifndef DARKTEC_CHARGE_ON_MA
#define DARKTEC_CHARGE_ON_MA  120.0f
#endif
#ifndef DARKTEC_CHARGE_OFF_MA
#define DARKTEC_CHARGE_OFF_MA 100.0f
#endif
// 3-й канал INA — ток самого пакета. Именно его интегрирует счётчик мАч:
// зарядник отдаёт больше, чем доходит до банки (часть уходит на питание ноды).
#ifndef DARKTEC_INA_BATT_CH
#define DARKTEC_INA_BATT_CH   2
#endif
// Порог «зарядник подключён» по напряжению шины CHRG. Пока зарядник есть,
// счётчик копит; когда пропал — замирает до следующего подключения.
#ifndef DARKTEC_CHARGE_DETECT_V
#define DARKTEC_CHARGE_DETECT_V 4.5f
#endif

// Тик интегрирования тока. Не привязан к обновлению экрана: счётчик живёт в
// loop() и считает с этой частотой независимо от того, что показывает дисплей.
// 250 мс — один I2C-замер на тик, ошибка прямоугольного суммирования на
// порядок ниже, чем нестабильность самого шунта.
#ifndef DARKTEC_CHARGE_TICK_MS
#define DARKTEC_CHARGE_TICK_MS     250
#endif
// Пропуск длиннее этого — залипший суперцикл (запись во флеш, рывок BLE).
// Промежуток не выбрасывается, а обрезается до этой величины: выбросить тик
// значит молча потерять заряд, а обрезать — учесть его по последнему замеру.
#ifndef DARKTEC_CHARGE_MAX_GAP_MS
#define DARKTEC_CHARGE_MAX_GAP_MS  5000
#endif

// ---- Discharge model -------------------------------------------------------
// Ток разряда на этой плате измерить нечем, поэтому он моделируется: тик
// умножается на ток текущего состояния. Значения измерены мультиметром на
// этой ревизии; после правок железа их надо перемерить.
#ifndef DARKTEC_I_DISPLAY_ON_MA
#define DARKTEC_I_DISPLAY_ON_MA    33.0f    // экран горит, радио в приёме
#endif
#ifndef DARKTEC_I_DISPLAY_OFF_MA
#define DARKTEC_I_DISPLAY_OFF_MA   28.0f    // экран погашен, радио в приёме
#endif
// Передача E22 при TX 22 дБм. Импульс, поэтому считается не тиком, а по
// фактической длительности передачи — пакет бывает короче одного тика.
#ifndef DARKTEC_I_TX_MA
#define DARKTEC_I_TX_MA            1560.0f
#endif
// Паспортная ёмкость пакета. Служит только начальной точкой отсчёта: после
// первой зарядки счётчик берёт фактически залитые мАч как новое «полное».
#ifndef DARKTEC_BATT_CAPACITY_MAH
#define DARKTEC_BATT_CAPACITY_MAH  3000.0f
#endif
// Ток, ниже которого зарядник считается завершившим заряд (примерно C/20 для
// пакета 3000 мАч), и сколько он должен держаться, чтобы это не был провал.
// Полный заряд — единственная точка, где счётчик достоверно знает остаток,
// поэтому именно здесь он прищёлкивается к 100% и сбрасывает накопленный дрейф.
#ifndef DARKTEC_CHARGE_FULL_MA
#define DARKTEC_CHARGE_FULL_MA       150.0f
#endif
#ifndef DARKTEC_CHARGE_FULL_HOLD_MS
#define DARKTEC_CHARGE_FULL_HOLD_MS  60000
#endif
// Постоянная времени сглаживания среднего потребления для ETA. Самодостаточна:
// не зависит от того, когда начался разряд, и переживает перезагрузку.
#ifndef DARKTEC_LOAD_TAU_MS
#define DARKTEC_LOAD_TAU_MS          600000UL   // 10 минут
#endif

class DarktecBoard : public NRF52BoardDCDC {
protected:
  uint8_t btn_prev_state;
  float adc_mult = ADC_MULTIPLIER;
  Adafruit_INA3221 _ina;
  bool _ina_ok = false;
  bool _charging = false;
  bool _txing = false;
  bool _rxing = false;
  uint32_t _ina_last_ms = 0;
  uint32_t _tx_hold_until = 0;
  uint32_t _rx_hold_until = 0;

  // Charge accounting, advanced by loop() once a second.
  float    _charge_mah = 0.0f;        // mAh poured into the pack this session
  bool     _charger_present = false;
  uint32_t _charge_last_ms = 0;
  uint32_t _charge_session_ms = 0;

  // Discharge accounting, advanced by the same tick while no charger is present.
  bool     _display_on = false;       // pushed in by the UI, picks the idle current
  uint32_t _tx_window_start = 0;
  uint32_t _tx_ms_pending = 0;        // on-air time accrued since the last tick
  float    _remain_mah = DARKTEC_BATT_CAPACITY_MAH;
  bool     _remain_anchored = false;  // false until seeded from prefs or voltage
  float    _capacity_mah = DARKTEC_BATT_CAPACITY_MAH;  // nameplate, set from Settings
  uint32_t _full_hold_ms = 0;         // how long the charge current has stayed low
  float    _avg_load_ma = DARKTEC_I_DISPLAY_ON_MA;     // smoothed draw, for the ETA
  float    _tx_ma = DARKTEC_I_TX_MA;  // draw of the fitted module, set from Settings

  void pollChargeSense();
  void seedRemainFromVoltage();
  bool readInaShuntReady();
#ifdef NRF52_POWER_MANAGEMENT
  void initiateShutdown(uint8_t reason) override;
#endif

public:
  DarktecBoard() : NRF52Board("Darktec_OTA") {}
  void begin();
  bool isCharging();
  bool isExternalPowered() override;

  // Periodic housekeeping, called from the example superloop. Keeps the charge
  // counter running whatever screen is on — and whatever the screen is doing,
  // including being off.
  void loop() override;

  // Charge counter for the INA page. mAh accumulated into the pack since the
  // current charger was attached; frozen while no charger is present, and
  // zeroed when the next one appears.
  uint32_t getChargeMah() const { return (uint32_t)(_charge_mah + 0.5f); }
  bool isChargerPresent() const { return _charger_present; }
  uint32_t getChargeSessionSecs() const { return _charge_session_ms / 1000; }

  // Discharge estimate for the battery test page.
  uint32_t getRemainMah() const override { return (uint32_t)(_remain_mah + 0.5f); }
  uint32_t getCapacityMah() const { return (uint32_t)(_capacity_mah + 0.5f); }
  // Smoothed draw, used for the ETA: the instantaneous value swings between
  // 28 mA and 1.5 A and would make the estimate jump about.
  float getAverageLoadMa() const { return _avg_load_ma; }
  // Current the model attributes to the present state, for eyeballing on screen.
  float getStateLoadMa() const {
    if (_txing) return _tx_ma;
    return _display_on ? DARKTEC_I_DISPLAY_ON_MA : DARKTEC_I_DISPLAY_OFF_MA;
  }
  uint32_t getEtaMinutes() const;

  void setDisplayOn(bool on) override { _display_on = on; }

  void setTxCurrentMa(float ma) override {
    if (ma > 0.0f) _tx_ma = ma;
  }

  void setRemainMah(uint32_t mah) override {
    // Seeded from prefs after a reboot. A zero means nothing was stored, so the
    // voltage fallback stays in charge instead.
    if (mah == 0) return;
    if ((float)mah > _capacity_mah) mah = (uint32_t)_capacity_mah;
    _remain_mah = (float)mah;
    _remain_anchored = true;
  }

  void setBatteryCapacityMah(float mah) override {
    if (mah < 100.0f || mah == _capacity_mah) return;
    // Carry the state of charge across the change: only the scale moves, so a
    // half-full pack stays half full when the nameplate figure is corrected.
    if (_remain_anchored && _capacity_mah > 0.0f) {
      _remain_mah = _remain_mah * (mah / _capacity_mah);
    }
    _capacity_mah = mah;
  }

  bool isLoRaActivity() const {
    return _txing || _rxing
        || (int32_t)(millis() - _tx_hold_until) < 0
        || (int32_t)(millis() - _rx_hold_until) < 0;
  }
  void setLoRaReceiving(bool v) { _rxing = v; }
  void onLoRaPacketReceived() { _rx_hold_until = millis() + 80; }
  void onBeforeTransmit() override {
    _txing = true;
    _tx_window_start = millis();
  }
  void onAfterTransmit() override {
    _txing = false;
    _tx_hold_until = millis() + 80;  // короткий хвост, чтобы вспышка была видна на OLED
    // Actual on-air window, measured here rather than sampled by the tick. A
    // packet can be shorter than one tick, and counting a whole tick would
    // overstate a brief 1.5 A pulse many times over.
    uint32_t dur = millis() - _tx_window_start;
    if (dur < 20000) _tx_ms_pending += dur;
  }

  #define BATTERY_SAMPLES 8

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / BATTERY_SAMPLES;
    return (uint16_t)(adc_mult * raw);
  }

  bool setAdcMultiplier(float multiplier) override {
    if (multiplier == 0.0f) {
      adc_mult = ADC_MULTIPLIER;}
    else {
      adc_mult = multiplier;
    }
    return true;
  }
  float getAdcMultiplier() const override {
    if (adc_mult == 0.0f) {
      return ADC_MULTIPLIER;
    } else {
      return adc_mult;
    }
  }

  const char* getManufacturerName() const override {
    return "Darktec";
  }

  int buttonStateChanged() {
    #ifdef BUTTON_PIN
      uint8_t v = digitalRead(BUTTON_PIN);
      if (v != btn_prev_state) {
        btn_prev_state = v;
        return (v == LOW) ? 1 : -1;
      }
    #endif
      return 0;
  }

  // Hibernate / CLI: SYSTEMOFF. Защита батареи — ADC-wait в initiateShutdown.
  void powerOff() override;
};

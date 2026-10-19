#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

// Пины по фактической распайке модуля LR2021 (20.10.2026). Номера слева —
// Arduino-пины, справа — нога nRF и её номер из g_ADigitalPinMap (variant.cpp).
#define P_LORA_SCLK  13 //P1.13 45
#define P_LORA_NSS   12 //P1.11 43
#define P_LORA_BUSY  14 //P1.15 47
#define P_LORA_MOSI  15 //P0.02 2
#define P_LORA_MISO  10 //P0.09 9
#define P_LORA_RESET 16 //P0.29 29
#define P_LORA_DIO_1 11 //P0.10 10 — IRQ модуля
#define RADIO_POWER_EN 21 //P0.13 13

/*
 * Радио: LR2021 (тестовая сборка на плате DarkTec).
 *
 *   SCK P1.13, NSS P1.11, MOSI P0.02, MISO P0.09
 *   RESET P0.29, BUSY P1.15, IRQ P0.10
 *   RADIO_POWER_EN 21 — питание модуля
 *
 * Нога CE (sleep) модуля не подключена: подтянута к плюсу внутри модуля.
 * Если радио не ответит по SPI — первым делом проверить её (притянуть к земле).
 *
 * DARKTEC_RADIO_CTRL_PIN (P0.17) — бывший пин 6 модуля E22, теперь никуда не
 * идёт. Держим высоким, чтобы не висел в воздухе.
 */
#define DARKTEC_RADIO_CTRL_PIN 2 // P0.17

#define  PIN_VBAT_READ 17
// Делитель батареи: 100к / 100к. Дефолт ADC_MULTIPLIER — в variant.h (1.750f).
#ifndef ADC_MULTIPLIER
#define  ADC_MULTIPLIER   (1.750f)
#endif

#include "DarktecAdcPower.h"
#include "DarktecPowerModel.h"
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
// Порог тока CHRG для индикатора на экране: выше — идёт заряд (молния), ниже —
// заряд закончился и зарядник держит подпитку (вилка). Напряжение CHRG при этом
// всё равно должно быть выше DARKTEC_CHARGE_DETECT_V, иначе иконки нет.
#ifndef DARKTEC_CHARGE_ICON_MA
#define DARKTEC_CHARGE_ICON_MA 50.0f
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
// Передача модуля при TX 22 дБм. Импульс, поэтому считается не тиком, а по
// фактической длительности передачи — пакет бывает короче одного тика.
// Значение снято с E22-900M33S: для LR2021 токи ещё не мерили.
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

  // Charge accounting, advanced by loop() once a second. Считает только
  // «сколько залито за сессию» для страницы INA — в SOC и ETA НЕ участвует.
  float    _charge_mah = 0.0f;        // mAh poured into the pack this session
  bool     _charger_present = false;
  uint8_t  _charge_icon = 0;   // 0 нет / 1 молния / 2 вилка
  uint32_t _charge_last_ms = 0;
  uint32_t _charge_session_ms = 0;
  uint32_t _full_hold_ms = 0;         // сколько ток заряда держится ниже порога

  // Состояние экрана и скважность передачи — входы модели питания.
  bool     _display_on = false;       // pushed in by the UI, picks the idle current
  uint32_t _tx_window_start = 0;

  // Модель питания: SOC из напряжения по реальной кривой и ETA интегрированием.
  // Токи LR2021 ещё не замерены — модель пока считает по профилю E22-900M33S
  // (см. комментарий в DarktecPowerModel.h).
  darktec::PowerModel _pm;
  int8_t   _tx_dbm = 22;              // выставленная мощность: нужна CLI и модели

  void pollChargeSense();
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
  // Индикатор заряда у значка батареи: 0 — нет, 1 — молния (идёт заряд),
  // 2 — вилка (заряд закончен, идёт подпитка). Считается по шине CHRG.
  uint8_t getChargeIndicator() const { return _charge_icon; }
  // Кабель в USB разъёме самой ноды (VBUS) — прошивка/консоль. Это не зарядка
  // пакета: у платы отдельный вход зарядника.
  bool isUsbConnected() { return NRF52Board::isExternalPowered(); }
  uint32_t getChargeSessionSecs() const { return _charge_session_ms / 1000; }

  // --- Модель питания: SOC, остаток, ETA -------------------------------------

  // Остаток ёмкости выводится из ПРОЦЕНТА, который взят из напряжения.
  // Залитые мАч сюда не входят: нода может заряжаться от 55% или 70%.
  uint32_t getRemainMah() const override { return _pm.getRemainMah(); }
  uint32_t getCapacityMah() const { return _pm.getCapacityMah(); }

  // Сколько процентов показывает модель (по кривой разряда, не линейно).
  uint8_t getSocPercent() const { return (uint8_t)(_pm.getSocPercent() + 0.5f); }
  // false, пока модель не получила ни одного пригодного замера напряжения:
  // UI в этом случае показывает грубую линейную оценку, а не пустой значок.
  bool powerModelReady() const { return _pm.hasSoc(); }
  // Идёт ли заряд прямо сейчас — по току пакета, а не по напряжению.
  bool isPowerModelCharging() const { return _pm.isCharging(); }
  // Напряжение покоя, по которому посчитан процент.
  uint16_t getOcvMilliVolts() const { return _pm.getOcvMilliVolts(); }

  // Средний ток за оставшийся разряд. Это честное среднее по остатку,
  // а не сглаженное мгновенное значение.
  float getAverageLoadMa() const { return _pm.averageLoadMa(); }
  // Ток, который модель приписывает текущему состоянию, для глаз на экране.
  float getStateLoadMa() const { return _pm.currentNowMa(_txing); }
  // Доля времени в эфире за последнее окно наблюдения (0..1).
  float getTxDutyFraction() const { return _pm.txDutyFraction(); }

  uint32_t getEtaMinutes() const { return _pm.etaMinutes(); }

  // Остаток до полного заряда (0, если заряда нет) — для страницы батареи.
  uint32_t getChargeEtaMinutes() const { return _pm.etaChargeMinutes(); }

  // Ток пакета как есть: под зарядом он положительный и показывает, сколько
  // реально льётся в банку (в отличие от модельного getStateLoadMa).
  float getBattCurrentMa() const { return _pm.getBattCurrentMa(); }

  // Мощность передатчика и ёмкость пакета — то, чем модель различает режимы.
  // Вызывается и из UI компаньона, и из CLI — чтобы у всех ролей было одно.
  void setPowerModel(int8_t tx_dbm, float capacity_mah) {
    _pm.setTxPowerDbm(tx_dbm);
    _pm.setCapacityMah(capacity_mah);
  }
  void setTxPowerDbm(int8_t tx_dbm) {
    _pm.setTxPowerDbm(tx_dbm);
    _tx_dbm = tx_dbm;
  }
  int8_t getTxPowerDbm() const { return _tx_dbm; }

  // Оставлено ради совместимости со старым интерфейсом MeshCore: значение,
  // сохранённое предыдущей прошивкой, теперь не используется — процент
  // берётся из напряжения.
  void setRemainMah(uint32_t mah) override { (void)mah; }

  void setDisplayOn(bool on) override {
    _display_on = on;
    _pm.setDisplayOn(on);
  }

  // Заставка: панель горит, но на контрасте 1 и с кадром раз в секунду —
  // это отдельный ток в модели, между «выключен» и «экран горит».
  void setDisplaySaver(bool on) { _pm.setDisplayState(on ? 1 : 0); }

  // Старый интерфейс: UI присылал ток модуля числом. Теперь модель берёт ток
  // из таблиц по мощности, поэтому число игнорируем.
  void setTxCurrentMa(float ma) override { (void)ma; }

  void setBatteryCapacityMah(float mah) override { _pm.setCapacityMah(mah); }

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
    // Фактическое время в эфире, измеренное здесь, а не тиком: пакет бывает
    // короче тика, и целый тик многократно завысил бы короткий импульс 1.5 А.
    // Уходит в модель как скважность передачи — она нужна для ETA.
    uint32_t dur = millis() - _tx_window_start;
    if (dur < 20000) _pm.noteTxTime(dur);
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

  // CLI-команды DarkTec. Компаньон разбирает команды сам и до CommonCLI
  // не доходит, поэтому держим их здесь — работает у всех ролей.
  bool handleCommand(const char* command, uint32_t sender_timestamp,
                     char* reply) override;
};

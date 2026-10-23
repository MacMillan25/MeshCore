#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <nrf_gpio.h>

#include "DarktecBoard.h"
#include "DarktecPrefs.h"
#include "target.h"
#include "DarktecFactoryCfg.h"
#include <helpers/NodePrefs.h>
#include <helpers/LowBattGuard.h>

// Фасад для BatteryPack: позволяет ядровому оверлею получить SOC нашей модели,
// не таща за собой объект платы (роль платы в main() отличается по примерам).
static DarktecBoard* g_power_board = nullptr;

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
    darktec::waitForBatteryRecovery(*this, RADIO_POWER_EN);
    return;  // недостижимо
  }
#endif

  digitalWrite(RADIO_POWER_EN, LOW);
  enterSystemOff(reason);
}
#endif


bool DarktecBoard::applyFactoryConfig(NodePrefs& prefs) {
  DarktecFactoryCfg::Payload p;
  const auto* blob = DarktecFactoryCfg::blob();
  if (!DarktecFactoryCfg::isValid(blob, p)) return false;
  if (p.flags == 0) return false;

  DarktecPrefsFormat::Settings s = DarktecPrefs::get();
  // Один и тот же блок применяем ровно один раз: CRC запоминаем в prefs.
  if (s.factory_crc == blob->crc32) return false;

  bool changed = false;

  if (p.flags & DarktecFactoryCfg::FLAG_NAME) {
    p.name[sizeof(p.name) - 1] = 0;
    if (p.name[0] != 0) {
      strncpy(prefs.node_name, (const char*)p.name, sizeof(prefs.node_name) - 1);
      prefs.node_name[sizeof(prefs.node_name) - 1] = 0;
      changed = true;
    }
  }

  if (p.flags & DarktecFactoryCfg::FLAG_RADIO) {
    prefs.freq = p.freq;
    prefs.bw = p.bw;
    prefs.sf = p.sf;
    prefs.cr = p.cr;
    prefs.tx_power_dbm = p.tx_dbm;
    _factory_radio_changed = true;
    _pm.setTxPowerDbm(p.tx_dbm);
    changed = true;
  }

  if (p.flags & DarktecFactoryCfg::FLAG_CAPACITY) {
    s.capacity_mode = p.capacity_mode;
    s.capacity_mah = p.capacity_mah;
    _pm.setCapacityMah((float)DarktecPrefsFormat::effectiveCapacityMah(s));
    changed = true;
  }

  if (p.flags & DarktecFactoryCfg::FLAG_MODULE) {
    // В этой сборке модуль не выбирается: узел всегда на LR2021. Байт храним
    // ради совместимости формата, на тракт и модель питания он не влияет.
    s.module = p.module;
    changed = true;
  }

  // CRC записываем всегда, даже если флаги ничего не поменяли: иначе блок
  // будет применяться на каждом старте.
  s.factory_crc = blob->crc32;
  DarktecPrefs::save(s);
  return changed;
}

// ДИАГНОСТИКА ГИБЕРНАЦИИ (тестовая сборка d8).
// Вместо SYSTEMOFF уходим в обычное ожидание с погашенным экраном: если нода и
// в этом режиме не поднимается по кнопке — значит виснет НЕ сон, а код
// выключения. Каждая стадия пишется в Serial, чтобы было видно, где встали.
// После проверки вернуть SYSTEMOFF (раскомментировать блок ниже).
// #define DARKTEC_HIBERNATE_SAFE 1

void DarktecBoard::powerOff() {
  // Гибернация. SYSTEMOFF сознательно НЕ используем: в нём разбудить чип можно
  // только по GPIO-детекту, LPCOMP, NFC или VBUS, а pin-reset оттуда не
  // поднимает — а нам нужно, чтобы нода включалась именно кнопкой RST.
  // Поэтому гасим периферию, выключаем тик и спим в WFE с живым ядром:
  // так работают оба пути (RST — обычный сброс, BTN — событие GPIOTE),
  // а потребление остаётся низким: тактирование и радио выключены.
#ifdef PIN_USER_BTN
  // Ждём отпускания кнопки, но не бесконечно: залипший LOW раньше подвешивал
  // ноду здесь навсегда.
  const uint32_t wait_started = millis();
  while (digitalRead(PIN_USER_BTN) == LOW && (millis() - wait_started) < 5000) {
    delay(10);
  }
#endif

  shutdownPeripherals();
  digitalWrite(RADIO_POWER_EN, LOW);

  // Гасим BLE-стек. Без этого SoftDevice будит ядро на каждый интервал
  // рекламы и держит радио включённым — в замере гибернация жрала 38 мА
  // вместо единиц. После выключения стека живём на голом WFE.
  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);
  if (sd_enabled) {
    sd_softdevice_disable();
    sd_enabled = 0;
  }
  MESH_DEBUG_PRINTLN("HIB: BLE stack stopped");

#ifdef PIN_USER_BTN
#  ifndef DARKTEC_HIBERNATE_BTN_WAKE
#    define DARKTEC_HIBERNATE_BTN_WAKE 1   // 0 — кнопка спящую ноду не будит
#  endif

  // Тик выключаем: иначе WFE просыпается каждую миллисекунду и жрёт ток.
  SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk;

#  if DARKTEC_HIBERNATE_BTN_WAKE
  nrf_gpio_cfg_input(g_ADigitalPinMap[PIN_USER_BTN], NRF_GPIO_PIN_PULLUP);
  NRF_GPIOTE->CONFIG[0] =
      ((uint32_t)GPIOTE_CONFIG_MODE_Event << GPIOTE_CONFIG_MODE_Pos) |
      ((uint32_t)g_ADigitalPinMap[PIN_USER_BTN] << GPIOTE_CONFIG_PSEL_Pos) |
      ((uint32_t)GPIOTE_CONFIG_POLARITY_HiToLo << GPIOTE_CONFIG_POLARITY_Pos);
  NRF_GPIOTE->EVENTS_IN[0] = 0;
  NRF_GPIOTE->INTENSET = GPIOTE_INTENSET_IN0_Msk;

  for (;;) {
    if (sd_enabled) {
      sd_app_evt_wait();
    } else {
      __WFE();
    }
    if (digitalRead(PIN_USER_BTN) == LOW) {
      NVIC_SystemReset();   // delay() здесь нельзя: SysTick уже остановлен
    }
  }
#  else
  // Тест: кнопка не будит, поднимает только RST.
  for (;;) {
    if (sd_enabled) {
      sd_app_evt_wait();
    } else {
      __WFE();
    }
  }
#  endif
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

    pinMode(RADIO_POWER_EN, OUTPUT);
#ifdef NRF52_POWER_MANAGEMENT
#if DARKTEC_BATT_PROTECT == DARKTEC_BATT_PROTECT_ADC
    // Boot-lock по АЦП пакета (химия): ждём подъёма VBAT на делителе.
    if (darktec::batteryBelowThreshold(*this, PWRMGT_VOLTAGE_BOOTLOCK)) {
      darktec::waitForBatteryRecovery(*this, RADIO_POWER_EN);
    }
#endif
    // LPCOMP runtime alert не включаем (lpcomp_low_refsel=0).
    (void)power_config;
#endif
    digitalWrite(RADIO_POWER_EN, HIGH);
    delay(10);   // дать модулю время на включение

    // Бывший пин 6 модуля E22 (P0.17). У LR2021-модуля это, скорее всего,
    // EN тракта: держим высоким постоянно, пока распайка не уточнена.
    pinMode(DARKTEC_RADIO_CTRL_PIN, OUTPUT);
    digitalWrite(DARKTEC_RADIO_CTRL_PIN, HIGH);

    readInaShuntReady();

    // Настройки DarkTec: из них берутся ёмкость пакета и порог отключения.
    g_power_board = this;
    DarktecPrefs::begin();
    // Диагностика: почему включились (RESETPIN / OFF / BOR / SREQ …).
  Serial.printf("BOOT: RESETREAS=0x%08X GPREGRET2=0x%02X\n",
                (unsigned)NRF_POWER->RESETREAS, (unsigned)NRF_POWER->GPREGRET2);

  // Блок заводских настроек: обращение к нему держит секцию в образе
    // (иначе --gc-sections её выбрасывает). Чтение появится отдельным шагом.
    (void)DarktecFactoryCfg::blob();
    const DarktecPrefsFormat::Settings& p = DarktecPrefs::get();
    // Порог отключения по низкому напряжению применяем сразу: проверка батареи
    // в main() идёт уже после board.begin(), поэтому стендовое значение из
    // prefs успевает подхватиться и переживает перезагрузку.
    LowBattGuard::set((uint16_t)p.low_batt_mv100 * 100);
    // Ёмкость — с учётом режима UserPref/3000/4000/RealCap: от неё масштабируются
    // остаток и ETA в модели.
    _pm.setCapacityMah((float)DarktecPrefsFormat::effectiveCapacityMah(p));
    // Цикл измерения ёмкости: нижняя точка и отметка «уже разрядились».
    // Отметка переживает перезагрузку — на 3.4 В нода может выключиться
    // раньше, чем воткнут зарядник. Модуль на этой ветке всегда LR2021,
    // поэтому setModule из prefs не зовём.
    _pm.setRealCapLowMv(DarktecPrefsFormat::realCapLowMv(p.real_cap_low_idx));
    _pm.setSawEmpty(p.saw_empty != 0);
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
  // сопротивлении, считается кулоновский заряд и рисуется Inow на экране.
  // Знак у INA3221 на разных платах разный, и модели он не нужен: заряд
  // решается по шине CHRG плюс модуль тока.
  float batt_ma = 0.0f;
  {
    float a = _ina.getCurrentAmps(DARKTEC_INA_BATT_CH);
    if (!isnan(a)) batt_ma = a * 1000.0f;
  }

  // Шина CHRG: по ней понимаем, подключён ли зарядник. Её же смотрит иконка.
  // Один замер на всё — лишний I2C на каждой секунде ни к чему.
  float chrg_v = _ina.getBusVoltage(DARKTEC_INA_CHARGE_CH);
  const bool present = isnan(chrg_v) ? _charger_present
                                     : (chrg_v > DARKTEC_CHARGE_DETECT_V);

  // Напряжение, ток, тишина в эфире и присутствие зарядника — в модель.
  _pm.update(getBattMilliVolts(), batt_ma, now, isLoRaActivity(), present);
  _pm.noteWindowTick(DARKTEC_CHARGE_TICK_MS);

  // Индикатор на экране: зарядник есть (шина CHRG под напряжением), а идёт ли
  // заряд — смотрим по току ПАКЕТА, канал BATT. Выше порога — молния, ниже —
  // заливка закончилась, зарядник просто держит подпитку (вилка).
  // Ток берём по модулю: знак зависит от полярности шунта на конкретной плате,
  // а индикатору важно только «идёт заряд или нет».
  if (present) {
    const float batt_abs = fabsf(batt_ma);
    // Гистерезис 15 мА: молния — выше 165 мА, вилка — ниже 150 мА. Без запаса
    // значок дребезжал на границе: ток плавает на ±10 мА.
    const float on_ma = DARKTEC_CHARGE_ICON_MA;
    const float off_ma = DARKTEC_CHARGE_ICON_OFF_MA;
    if (batt_abs > on_ma) {
      _charge_icon = 1;
    } else if (batt_abs < off_ma) {
      _charge_icon = 2;
    } else if (_charge_icon == 0) {
      _charge_icon = 2;   // зарядник только что подключили, ток ещё не нарос
    }
  } else {
    _charge_icon = 0;
  }

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
      const float batt_abs = fabsf(batt_ma);
      if (batt_abs > 0.0f) {
        // mA * мс / 3 600 000 == mAh. Только счётчик сессии для страницы INA.
        // Делитель считаем от миллиампер: пока ток был в амперах, хватало
        // 3600, а с мА это давало ровно в тысячу раз больше.
        _charge_mah += batt_abs * (float)dt / 3600000.0f;
        _charge_session_ms += dt;
      }
      // Долгий низкий ток при подключённом заряднике — банка взяла всё.
      if (batt_abs < DARKTEC_CHARGE_FULL_MA) {
        _full_hold_ms += dt;
      } else {
        _full_hold_ms = 0;
      }
    }
  }

  _charger_present = present;
}

bool DarktecBoard::isCharging() {
  // «Зарядник подключён», а не «идёт заряд»: решение принимается по шине CHRG
  // в loop(), туда же смотрит иконка. По току это врало, когда зарядник брал
  // своё и ток уходил ниже порога, а знак шунта на разных платах разный.
  return _charger_present;
}

bool DarktecBoard::isExternalPowered() {
  // Зарядка пакета — INA3221 CH2. USB VBUS MCU к зарядке не относится,
  // но для KEEP_DISPLAY / CLI «external» учитываем и его (DFU, кабель).
  return isCharging() || NRF52Board::isExternalPowered();
}

// Фасад состояния питания для BatteryPack (см. DarktecPowerState.h).
// Пока модель не получила ни одного пригодного замера — линейная оценка по
// верхней и нижней точке пакета, чтобы значок не мигал пустым на старте.
namespace DarktecPowerState {

uint8_t socPercent(uint16_t fallback_pack_mv) {
  if (g_power_board != nullptr && g_power_board->powerModelReady()) {
    return g_power_board->getSocPercent();
  }
  if (fallback_pack_mv >= (uint16_t)BATT_MAX_MILLIVOLTS) return 100;
  if (fallback_pack_mv <= (uint16_t)BATT_MIN_MILLIVOLTS) return 0;
  return (uint8_t)((uint32_t)(fallback_pack_mv - BATT_MIN_MILLIVOLTS) * 100 /
                   (uint32_t)(BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS));
}

bool chargeActive() {
  return g_power_board != nullptr && g_power_board->isPowerModelCharging();
}

}  // namespace DarktecPowerState

// CLI DarkTec. Компаньон разбирает команды в MyMesh::handleCommand и до
// CommonCLI не доходит, поэтому команды живут на плате — работают у всех ролей.
bool DarktecBoard::handleCommand(const char* command, uint32_t sender_timestamp,
                                 char* reply) {
  (void)sender_timestamp;
  DarktecPrefsFormat::Settings s = DarktecPrefs::get();

  // `lr2021` — что видит драйвер: статус прерываний чипа, RSSI, счётчики.
  // `lr2021 scan [сек]` — посчитать, что приходит из эфира (по умолчанию 5 с).
  // `lr2021 rf N` — вариант таблицы RF-ключа (1 основной, 2 питание постоянно,
  //                3 все DIO в нуле).
  // `lr2021 dio N 0|1` — вручную выставить ногу DIO чипа.
  // `lr2021 irq N` — перевести ногу N в прерывание и перезапустить приём.
  // reply у ролей разного размера (у компаньона 320, у репитера 160), поэтому
  // во все функции передаём 150 байт — строки короче.
  const size_t rlen = 150;
  {
    int a = 0, b = 0;
    if (memcmp(command, "lr2021 none", 11) == 0) {
      radio_dio_release();
      snprintf(reply, rlen, "OK dio Hi-Z");
      return true;
    }
    if (sscanf(command, "lr2021 boost %d", &a) == 1) {
      radio_set_boost(a != 0, reply, rlen);
      return true;
    }
    if (sscanf(command, "lr2021 listen %d", &a) == 1) {
      radio_listen((uint8_t)((a < 1 || a > 30) ? 10 : a), reply, rlen);
      return true;
    }
    if (sscanf(command, "lr2021 rx %d", &a) == 1) {
      radio_set_rx_mask((uint8_t)a);
      snprintf(reply, rlen, "OK rxm=%u", (unsigned)radio_rx_mask());
      return true;
    }
    if (sscanf(command, "lr2021 sweep %d", &a) == 1 || memcmp(command, "lr2021 sweep", 12) == 0) {
      radio_sweep((uint8_t)((a < 1 || a > 5) ? 2 : a), reply, rlen);
      return true;
    }
    if (sscanf(command, "lr2021 dio %d %d", &a, &b) == 2) {
      radio_dio((uint8_t)a, b != 0);
      snprintf(reply, rlen, "OK dio %d = %d", a, b != 0 ? 1 : 0);
      return true;
    }
    if (sscanf(command, "lr2021 irq %d", &a) == 1) {
      radio_set_irq((uint8_t)a);
      snprintf(reply, rlen, "OK irq dio=%u", (unsigned)radio_irq_dio());
      return true;
    }
    if (sscanf(command, "lr2021 rf %d", &a) == 1) {
      radio_apply_rf((uint8_t)a);
      snprintf(reply, rlen, "OK rf=%u", (unsigned)radio_rf_preset());
      return true;
    }
  }
  if (memcmp(command, "lr2021 scan", 11) == 0) {
    const int secs = atoi(&command[11]);
    radio_scan((uint8_t)((secs < 1 || secs > 30) ? 5 : secs), reply, rlen);
    return true;
  }
  if (memcmp(command, "lr2021 spi", 10) == 0) {
    radio_spi_test(reply, rlen);
    return true;
  }
  if (memcmp(command, "lr2021 tx", 9) == 0) {
    const int n = atoi(&command[9]);
    radio_tx_test((uint8_t)((n < 1 || n > 10) ? 1 : n), reply, rlen);
    return true;
  }
  if (memcmp(command, "lr2021", 6) == 0) {
    radio_diag(reply, rlen);
    return true;
  }

  // Порог отключения по низкому напряжению — только на текущую сессию, в файл
  // не пишется: после перезагрузки защита снова штатная (3.40 В). Нужно для
  // стенда, где нода питается от ЛБП ниже этого уровня.
  if (memcmp(command, "set darktec.lowbat ", 20) == 0) {
    const char* v = &command[20];
    if (memcmp(v, "off", 3) == 0 || memcmp(v, "default", 7) == 0) {
      s.low_batt_mv100 = 0;
      DarktecPrefs::save(s);
      LowBattGuard::clear();
      strcpy(reply, "OK - lowbat default 3400 mV (saved)");
      return true;
    }
    float volts = atof(v);
    if (volts < 1.0f || volts > 4.0f) {
      strcpy(reply, "Error: volts 1.0..4.0");
      return true;
    }
    s.low_batt_mv100 = (uint8_t)(volts * 10.0f + 0.5f);
    DarktecPrefs::save(s);
    LowBattGuard::set((uint16_t)s.low_batt_mv100 * 100);
    sprintf(reply, "OK - cutoff %u mV (saved)", (unsigned)LowBattGuard::millivolts());
    return true;
  }
  if (memcmp(command, "get darktec.lowbat", 18) == 0) {
    if (LowBattGuard::overridden()) sprintf(reply, "> %u mV", (unsigned)LowBattGuard::millivolts());
    else strcpy(reply, "> default 3400 mV");
    return true;
  }
  if (memcmp(command, "set darktec.capacity ", 21) == 0) {
    int mah = atoi(&command[21]);
    if (mah < DarktecPrefsFormat::kCapacityMin ||
        mah > DarktecPrefsFormat::kCapacityMax) {
      sprintf(reply, "Error: capacity %u..%u",
              (unsigned)DarktecPrefsFormat::kCapacityMin,
              (unsigned)DarktecPrefsFormat::kCapacityMax);
      return true;
    }
    s.capacity_mah = (uint16_t)mah;
    s.capacity_mode = DarktecPrefsFormat::CAP_USER;
    DarktecPrefs::save(s);
    setBatteryCapacityMah((float)DarktecPrefsFormat::effectiveCapacityMah(s));
    sprintf(reply, "OK - %d mAh", mah);
    return true;
  }
  if (memcmp(command, "set darktec.caplow ", 19) == 0) {
    const uint16_t mv = (uint16_t)(atof(&command[19]) * 1000.0f + 0.5f);
    uint8_t idx;
    if (mv == 3000)      idx = DarktecPrefsFormat::RCAP_LOW_3000;
    else if (mv == 3200) idx = DarktecPrefsFormat::RCAP_LOW_3200;
    else if (mv == 3400) idx = DarktecPrefsFormat::RCAP_LOW_3400;
    else {
      strcpy(reply, "Error: caplow 3.0|3.2|3.4");
      return true;
    }
    s.real_cap_low_idx = idx;
    DarktecPrefs::save(s);
    setRealCapLowMv(mv);
    sprintf(reply, "OK - %u mV", (unsigned)mv);
    return true;
  }
  if (memcmp(command, "set darktec.capmode ", 20) == 0) {
    const char* v = &command[20];
    if (memcmp(v, "user", 4) == 0) {
      s.capacity_mode = DarktecPrefsFormat::CAP_USER;
    } else if (memcmp(v, "real", 4) == 0) {
      s.capacity_mode = DarktecPrefsFormat::CAP_REAL;
    } else if (atoi(v) == 3000) {
      s.capacity_mode = DarktecPrefsFormat::CAP_3000;
    } else if (atoi(v) == 4000) {
      s.capacity_mode = DarktecPrefsFormat::CAP_4000;
    } else {
      strcpy(reply, "Error: capmode real|user|3000|4000");
      return true;
    }
    DarktecPrefs::save(s);
    setBatteryCapacityMah((float)DarktecPrefsFormat::effectiveCapacityMah(s));
    sprintf(reply, "OK - %u mAh",
            (unsigned)DarktecPrefsFormat::effectiveCapacityMah(s));
    return true;
  }
  if (memcmp(command, "set adc.multiplier ", 19) == 0) {
    setAdcMultiplier(atof(&command[19]));
    strcpy(reply, "OK");
    return true;
  }
  return false;
}

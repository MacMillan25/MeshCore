/*
 * DarktecPowerModel.h — модель питания DarkTec: SOC из напряжения и ETA.
 *
 * Считает две вещи, и обе — не по-линейному:
 *
 *   1. Процент заряда (SOC) из напряжения пакета по РЕАЛЬНОЙ кривой разряда
 *      Li-ion, а не по прямой между пустым и полным. Кривая — в
 *      DarktecPowerTables.h, снята с живой банки на токе 100 мА. В середине
 *      диапазона она почти горизонтальна: 3-5 мВ на 1% SOC.
 *
 *   2. ETA — ИНТЕГРИРОВАНИЕМ по кривой, а не делением остатка на средний ток.
 *      Ток ноды сам зависит от напряжения (25.7 мА при 4.2 В и 50 мА при 3.0 В
 *      на 33S), поэтому «остаток / средний ток» систематически врёт.
 *
 * Принципиально: залитые мАч в расчёте НЕ участвуют. Нода может заряжаться
 * от 55% или 70% — процент берётся из напряжения, остаток считается от него.
 *
 * Напряжение берётся ТОЛЬКО в покое: под нагрузкой 1.5 А клеммы просажены
 * (внутреннее сопротивление + защита), а в полке кривой 3-5 мВ на 1% SOC,
 * так что замер под передачей уводит процент на единицы.
 */

#pragma once

#include <math.h>
#include <stdint.h>

#include <DarktecPowerTables.h>

namespace darktec {

// Индексы модулей — порядок обязан совпадать с таблицами в DarktecPowerTables.h.
// LR2021F33-2G4 замерен 23.10.2026: покой, экран, заставка и TX 22 dBm; остальные
// мощности TX пока расчётные (пропорция от 33S по замеру на 22 dBm).
enum PowerModule : uint8_t {
  MODULE_E22_900M33S  = 0,
  MODULE_E22_900M30S  = 1,
  MODULE_HT_RA62      = 2,
  MODULE_E22P_868M30S = 3,
  MODULE_LR2021_1W    = 4,
  MODULE_COUNT        = 5
};

// Сколько держать тишину в эфире, прежде чем верить напряжению как напряжению
// покоя. Короткая передача и хвост приёма (80 мс) сюда не попадают.
static const uint32_t OCV_QUIET_MS = 3000;

// Шаг интегрирования ETA по SOC.
static const uint8_t ETA_SOC_STEP = 1;

// Потолок ETA, минут (~4.5 месяца) — чтобы не гонять цикл впустую.
static const uint32_t ETA_MINUTES_CAP = 200000UL;

// Ток пакета, ниже которого заряд не считаем: INA3221 шумит на десятки мА.
static const float CHARGE_DETECT_MA = 20.0f;
// Хвост заряда (ток ниже 150 мА) не прогнозируем: последние ~100 мАч
// заливаются часами, и любой прогноз там только врёт. Совпадает с порогом
// «вилки» в DarktecBoard (DARKTEC_CHARGE_ICON_OFF_MA).
static const float CHARGE_TAIL_MA = 150.0f;
// Минимум для среднего тока в ETA: не даём прогнозу раздуться на спаде.
static const float CHARGE_ETA_MIN_MA = 150.0f;

// --- Измеренная ёмкость (RealCap) -------------------------------------------
// Конец заливки: ток пакета (измеренный) ниже 200 мА — значит CV-хвост, считать
// дальше нечего, за это время в банку уходит всё меньше.
static const float REAL_CAP_END_MA = 200.0f;
// Накоплено меньше — считаем шумом/сбоем и ёмкость не обновляем.
static const float REAL_CAP_MIN_MAH = 500.0f;

// Сглаживание показаний. Замеры идут раз в 250 мс, а АЦП шумит на пару
// милливольт — в полке кривой это доли процента, но остаток и ETA от них
// дёргаются. Экспоненциальное сглаживание убирает дрожь, не мешая реальному
// изменению за минуты.
static const float SOC_SMOOTH_ALPHA = 0.05f;   // τ ≈ 5 с при тике 250 мс
static const float ETA_SMOOTH_ALPHA = 0.02f;   // τ ≈ 12 с

// Ограничение прыжка ETA за один тик: не быстрее 1% (и не меньше 1 минуты),
// чтобы смена скважности не перебрасывала прогноз на часы одним кадром.
static const float ETA_MAX_STEP_FRACTION = 0.01f;

class PowerModel {
public:
  void setCapacityMah(float mah) {
    if (mah < 100.0f) return;
    _capacity_mah = mah;
    _eta_valid = false;   // ёмкость поменялась — прогноз начинаем заново
  }

  void setTxPowerDbm(int8_t dbm) {
    // Вызывается каждый цикл из UI, поэтому сбрасываем сглаживание только при
    // реальной смене мощности — иначе ETA никогда не успокоится.
    if (dbm == _tx_dbm) return;
    _tx_dbm = dbm;
    _eta_valid = false;
  }

  // Состояние экрана: три разных тока покоя. 0 — выключен, 1 — заставка
  // (контраст 1, кадр раз в секунду), 2 — обычный экран.
  void setDisplayState(uint8_t state) { _display_state = (state > 2) ? 2 : state; }
  void setDisplayOn(bool on) { _display_state = on ? 2 : 0; }

  uint32_t getCapacityMah() const { return (uint32_t)(_capacity_mah + 0.5f); }

  // --- SOC ------------------------------------------------------------------

  // Периодический вызов (раз в 250 мс): ток, состояние заряда и напряжение.
  //
  // batt_current_ma — ток пакета от INA3221, знак БЕРЁМ КАК ЕСТЬ и ни на что
  // его не тратим: у разных плат шунт впаян по-разному, а угадывание знака
  // один раз при старте давало обратный результат, если нода включилась уже
  // на зарядке. Отсюда и брались «CHG done» на разряде и «висящие 44 мА».
  //
  // Заряд определяется двумя независимыми признаками:
  //   charger_present — напряжение шины CHRG выше порога (то же, по чему
  //                     рисуется значок молнии/вилки);
  //   |ток пакета|     — выше порога, значит в банку реально что-то льётся.
  // Знак тока при этом не нужен вовсе.
  void update(uint16_t batt_mv, float batt_current_ma, uint32_t now_ms,
              bool radio_active, bool charger_present) {
    // Ток и флаг заряда обновляем ВСЕГДА, даже когда в эфире трафик: экран и
    // кулоновский счёт должны видеть свежий замер, а не застывшее число.
    _batt_current_ma = batt_current_ma;
    _current_known = true;

    const float i_abs = fabsf(batt_current_ma);
    // Состояние заряда держится, пока зарядник подключён: в конце заливки ток
    // падает до нуля, и без защёлки экран прыгал бы с «CHG done» на разрядный
    // прогноз и обратно на каждом мигании тока.
    const bool charging = charger_present && (i_abs > CHARGE_DETECT_MA || _charging);
    _charging = charging;

    // --- измеренная ёмкость ---------------------------------------------
    // Цикл считается полным, если перед зарядкой разрядились до выбранного
    // порога (3.0/3.2/3.4 В), а в конце дошли до 4.15 В при измеренном токе
    // ниже REAL_CAP_END_MA. Тогда заряд сессии и есть реальная ёмкость.
    //
    // Порог сравниваем с напряжением ПОКОЯ (_ocv_mv), а не с сырым замером:
    // под передачей клеммы просаживаются на сотни милливольт, и на почти
    // пустой банке сырой замер ложно «касался» бы порога.
    if (!charging && _ocv_mv > 0 && _ocv_mv <= _real_cap_low_mv) {
      if (!_saw_empty) { _saw_empty = true; _saw_empty_dirty = true; }
      _real_cap_acc = 0.0f;
    }
    if (charging) {
      if (!_real_cap_tracking) { _real_cap_tracking = true; _real_cap_last_ms = now_ms; }
      const uint32_t dtc = now_ms - _real_cap_last_ms;
      _real_cap_last_ms = now_ms;
      // INA видит весь ток, который идёт от зарядника, а нода в это время
      // питается от него же. В банку попадает разница: ток заряда минус
      // собственное потребление (его модель знает по таблицам).
      float net_ma = i_abs - currentNowMa(radio_active);
      if (net_ma < 0.0f) net_ma = 0.0f;
      if (dtc > 0 && dtc <= 60000) _real_cap_acc += net_ma * ((float)dtc / 3600000.0f);
      if (_saw_empty && batt_mv >= 4150 && i_abs < REAL_CAP_END_MA &&
          _real_cap_acc > REAL_CAP_MIN_MAH) {
        _real_cap_mah = (uint16_t)(_real_cap_acc + 0.5f);
        _real_cap_dirty = true;
        _saw_empty = false;
        _saw_empty_dirty = true;
      }
    } else {
      _real_cap_tracking = false;
    }

    if (radio_active) _last_active_ms = now_ms;

    if (charging) {
      // Кулоновский счёт. Напряжение под зарядом врёт (2 А поднимают клеммы
      // почти до 4.2 В), поэтому процент и остаток берутся из счётчика.
      chargedTick(batt_mv, i_abs, now_ms);
      smoothEta();
      smoothChargeEta(now_ms);
      return;
    }

    // Зарядки нет: счётчик закрыт, прогноз заряда сброшен здесь же.
    _charge_track = false;

    // Дальше — только напряжение покоя. Под передачей клеммы просажены
    // (1.5 А), а в полке кривой 3-5 мВ на 1% SOC, поэтому замер под нагрузкой
    // игнорируем, пока эфир не утихнет.
    smoothEta();
    smoothChargeEta(now_ms);
    if (radio_active) return;
    if (_last_active_ms != 0 && (uint32_t)(now_ms - _last_active_ms) < OCV_QUIET_MS) {
      return;
    }
    if (batt_mv < 1500) return;   // явный мусор в замере

    // Разряд: клеммы ниже напряжения банки, компенсация поднимает вверх.
    _ocv_mv = ocvFromTerminal(batt_mv, -i_abs);
    const float soc_now = socFromVoltage(_ocv_mv);
    if (!_soc_valid) {
      _soc = soc_now;
      _soc_valid = true;
    }
    // Разряд: процент по кривой, сглаженный. Одиночный шумный замер не должен
    // дёргать остаток в мАч и ETA.
    _soc += (soc_now - _soc) * SOC_SMOOTH_ALPHA;
  }

  bool isCharging() const { return _charging; }

  uint16_t getRealCapMah() const { return _real_cap_mah; }
  void     setRealCapMah(uint16_t v) { _real_cap_mah = v; }
  bool     takeRealCapDirty() { bool d = _real_cap_dirty; _real_cap_dirty = false; return d; }

  // Нижняя точка старта цикла RealCap (3.0/3.2/3.4 В) и отметка «уже разрядились».
  void     setRealCapLowMv(uint16_t mv) { _real_cap_low_mv = mv; }
  uint16_t realCapLowMv() const { return _real_cap_low_mv; }
  bool     sawEmpty() const { return _saw_empty; }
  void     setSawEmpty(bool v) { _saw_empty = v; _saw_empty_dirty = false; }
  bool     takeSawEmptyDirty() { bool d = _saw_empty_dirty; _saw_empty_dirty = false; return d; }
  float getBattCurrentMa() const { return _batt_current_ma; }
  // Был ли хоть один замер тока: до первого замера UI нечего показывать.
  bool isCurrentKnown() const { return _current_known; }

  bool hasSoc() const { return _soc_valid; }
  float getSocPercent() const { return _soc; }
  uint16_t getOcvMilliVolts() const { return _ocv_mv; }
  uint32_t getRemainMah() const {
    return (uint32_t)(_capacity_mah * _soc / 100.0f + 0.5f);
  }

  // Напряжение пакета, соответствующее текущему SOC (для диагностики).
  uint16_t getSocVoltage() const { return voltageForSoc(_soc, vFullMv()); }

  // --- ТОК ------------------------------------------------------------------

  // Ток, который модель приписывает текущему состоянию, мА.
  float currentNowMa(bool transmitting) const {
    return transmitting ? txCurrentAt(_ocv_mv) : idleCurrentAt(_ocv_mv);
  }

  // Средний ток за оставшийся разряд, мА. Это то, что показывается как Iavg:
  // не сглаженное мгновенное значение, а честное среднее по остатку.
  float averageLoadMa() const {
    uint32_t eta = etaMinutes();
    float remain = (float)getRemainMah();
    if (eta == 0 || remain <= 0.0f) return currentNowMa(false);
    return remain / ((float)eta / 60.0f);
  }

  // --- ETA ------------------------------------------------------------------

  // Остаток времени до пустой банки. Значение сглаженное: UI читает его
  // несколько раз в секунду, а сырой прогноз зависит от скважности и шума.
  uint32_t etaMinutes() const {
    if (_eta_valid) return (uint32_t)(_eta_smooth + 0.5f);
    return (uint32_t)(computeEtaMinutes(_soc) + 0.5f);
  }

  // Остаток времени до полного заряда; 0, если заряда нет.
  // На CC-фазе прогноз честный (ток постоянный), на CV ток падает, поэтому
  // после 80% он намеренно растянут — иначе обещает «10 минут» и висит час.
  uint32_t etaChargeMinutes() const {
    if (_charge_eta_valid && _charging) {
      return (uint32_t)(_charge_eta_smooth + 0.5f);
    }
    return 0;
  }

  // Наблюдаемая доля времени в эфире.
  float txDutyFraction() const { return _tx_fraction; }

  // --- Наблюдаемая скважность передачи --------------------------------------

  // Вызывается по факту передачи: сколько миллисекунд нода молотила в эфир.
  void noteTxTime(uint32_t ms) { _tx_ms_in_window += ms; }

  // Вызывается регулярно, чтобы окно наблюдения продвигалось.
  void noteWindowTick(uint32_t ms) {
    _window_ms += ms;
    if (_window_ms >= TX_WINDOW_MS) {
      float f = (float)_tx_ms_in_window / (float)_window_ms;
      if (f > 0.5f) f = 0.5f;      // выше половины времени передачи не бывает
      _tx_fraction = f;
      _window_ms = 0;
      _tx_ms_in_window = 0;
    }
  }

private:
  static const uint32_t TX_WINDOW_MS = 600000UL;   // 10 минут

  // Прогноз до пустой банки от заданного SOC. Отдельный метод, чтобы
  // сглаживание могло пересчитать его, не трогая отображаемый _soc.
  float computeEtaMinutes(float soc_start) const {
    if (!_soc_valid || soc_start <= 0.0f || _capacity_mah <= 0.0f) return 0.0f;

    const float tx_fraction = _tx_fraction;
    float minutes = 0.0f;
    float soc = soc_start;

    while (soc > 0.5f) {
      float step = (soc < (float)ETA_SOC_STEP) ? soc : (float)ETA_SOC_STEP;
      float soc_mid = soc - step * 0.5f;
      uint16_t mv = voltageForSoc(soc_mid, vFullMv());

      float i_idle = idleCurrentAt(mv);
      float i_tx = txCurrentAt(mv);
      float i_avg = i_idle + (i_tx - i_idle) * tx_fraction;
      if (i_avg < 1.0f) i_avg = 1.0f;

      minutes += (_capacity_mah * step / 100.0f) / i_avg * 60.0f;
      soc -= step;

      if (minutes > (float)ETA_MINUTES_CAP) return (float)ETA_MINUTES_CAP;
    }
    return minutes;
  }

  // Остаток пути до полного, делённый на текущий ток заряда. Остаток ведётся
  // кулоновским счётом от момента подключения зарядника, поэтому CV-фазу
  // (ток падает) формула отрабатывает сама: числитель уменьшается, знаменатель
  // тоже, и прогноз не приходится растягивать коэффициентами.
  float computeChargeEtaMinutes(uint32_t now_ms) const {
    if (!_soc_valid || !_charging || !_charge_track) return 0.0f;
    const float i_ma = fabsf(_batt_current_ma);
    if (i_ma < CHARGE_TAIL_MA) return 0.0f;      // вилка: заливка закончилась
    const float remain_mah = _capacity_mah - _charge_mah;
    if (remain_mah <= 0.0f) return 0.0f;

    // Средний ток за сессию, а не мгновенный: на CV мгновенный падает и даёт
    // скачок прогноза вверх (14 -> 44 минуты). Средний меняется плавно.
    float i_eta = i_ma;
    const float elapsed_h = (float)(now_ms - _charge_start_ms) / 3600000.0f;
    const float charged_mah = _charge_mah - _charge_start_mah;
    if (elapsed_h > 0.03f && charged_mah > 1.0f) {
      const float i_avg = charged_mah / elapsed_h;
      if (i_avg > i_eta) i_eta = i_avg;
    }
    if (i_eta < CHARGE_ETA_MIN_MA) i_eta = CHARGE_ETA_MIN_MA;
    return remain_mah / i_eta * 60.0f;
  }

  // Сглаживание ETA: экспонента плюс ограничение прыжка за тик.
  void smoothEta() {
    const float raw = computeEtaMinutes(_soc);
    if (!_eta_valid) {
      _eta_smooth = raw;
      _eta_valid = true;
      return;
    }
    float step = (raw - _eta_smooth) * ETA_SMOOTH_ALPHA;
    const float max_step = _eta_smooth * ETA_MAX_STEP_FRACTION + 1.0f;
    if (step > max_step) step = max_step;
    if (step < -max_step) step = -max_step;
    _eta_smooth += step;
    if (_eta_smooth < 0.0f) _eta_smooth = 0.0f;
  }

  void smoothChargeEta(uint32_t now_ms) {
    if (!_charging) {
      _charge_eta_smooth = 0.0f;
      _charge_eta_valid = false;
      return;
    }
    const float raw = computeChargeEtaMinutes(now_ms);
    if (!_charge_eta_valid) {
      _charge_eta_smooth = raw;
      _charge_eta_valid = true;
      return;
    }
    float step = (raw - _charge_eta_smooth) * ETA_SMOOTH_ALPHA;
    const float max_step = _charge_eta_smooth * ETA_MAX_STEP_FRACTION + 1.0f;
    if (step > max_step) step = max_step;
    if (step < -max_step) step = -max_step;
    _charge_eta_smooth += step;
    if (_charge_eta_smooth < 0.0f) _charge_eta_smooth = 0.0f;
  }

  float    _capacity_mah = 3000.0f;
  float    _soc = 100.0f;
  bool     _soc_valid = false;
  bool     _charging = false;

  // измеренная ёмкость: флаг «разрядились до порога» и накопитель заряда
  bool     _saw_empty = false;
  bool     _saw_empty_dirty = false;   // отметка изменилась — надо сохранить в prefs
  uint16_t _real_cap_low_mv = 3400;    // нижняя точка старта цикла, мВ
  bool     _real_cap_tracking = false;
  bool     _real_cap_dirty = false;
  float    _real_cap_acc = 0.0f;
  uint32_t _real_cap_last_ms = 0;
  uint16_t _real_cap_mah = 0;
  // Кулоновский счёт во время заряда: остаток фиксируется на старте заряда и
  // дальше только прибавляется по измеренному току. Напряжение банки под
  // зарядом врёт (2 А поднимают клеммы почти до 4.2 В), поэтому процент и
  // остаток в это время берутся отсюда, а не из кривой.
  bool     _charge_track = false;
  uint32_t _charge_last_ms = 0;
  uint32_t _charge_start_ms = 0;     // начало сессии заряда (для среднего тока)
  float    _charge_mah = 0.0f;       // накопленный остаток
  float    _charge_start_mah = 0.0f; // остаток на момент подключения зарядника
  bool     _current_known = false;
  float    _batt_current_ma = 0.0f;
  uint16_t _ocv_mv = 0;
  uint8_t  _module = MODULE_LR2021_1W;  // LR2021: замеров нет, таблицы от 33S
  int8_t   _tx_dbm = 22;
  uint8_t  _display_state = 0;   // 0 выкл, 1 заставка, 2 экран
  uint32_t _last_active_ms = 0;

  float    _eta_smooth = 0.0f;
  bool     _eta_valid = false;
  float    _charge_eta_smooth = 0.0f;
  bool     _charge_eta_valid = false;

  uint32_t _tx_ms_in_window = 0;
  uint32_t _window_ms = 0;
  float    _tx_fraction = 0.0f;

  // --- окно напряжений по химии пакета --------------------------------------

  static uint16_t vFullMv() {
#if defined(BATTERY_CHEMISTRY) && BATTERY_CHEMISTRY == BATTERY_CHEM_LIFEPO4
    return V_FULL_LIFEPO4_MV;
#elif defined(BATTERY_CHEMISTRY) && BATTERY_CHEMISTRY == BATTERY_CHEM_LTO
    return V_FULL_LTO_MV;
#else
    return V_FULL_LIION_MV;
#endif
  }

  // --- компенсация падения на внутреннем сопротивлении ----------------------

  // Клеммы под током показывают не напряжение банки: 2 А заряда поднимают их
  // на ~0.5 В, и без компенсации нода радостно показывает 100% при 50%
  // реальных. Отсюда V_разомкн = V_изм - I*R, где «+» — ток в пакет, «−» — из
  // пакета. Знак берётся не из измерения, а из состояния заряда: под зарядом
  // ток течёт внутрь, иначе наружу.
  // R и коэффициент подобраны по двум реальным кривым (см. генератор).
  static uint16_t ocvFromTerminal(uint16_t batt_mv, float current_ma) {
    float i_a = current_ma / 1000.0f;
    float v = (float)batt_mv - i_a * IR_R_OHM * IR_K;
    if (v < 0.0f) v = 0.0f;
    return (uint16_t)(v + 0.5f);
  }

  // Тик под зарядом: кулоновский счёт и процент из него.
  void chargedTick(uint16_t batt_mv, float i_abs, uint32_t now_ms) {
    if (!_soc_valid && batt_mv >= 1500) {
      // Первый замер пришёл уже под зарядом: процент сеем по напряжению с
      // компенсацией падения, иначе счётчик стартовал бы от 100%.
      _ocv_mv = ocvFromTerminal(batt_mv, i_abs);
      _soc = socFromVoltage(_ocv_mv);
      _soc_valid = true;
    }
    if (!_charge_track) {
      // Начало заряда: запоминаем остаток по текущему проценту и дальше
      // живём только счётом кулонов.
      _charge_mah = _capacity_mah * _soc / 100.0f;
      _charge_start_mah = _charge_mah;
      _charge_start_ms = now_ms;
      _charge_track = true;
      _charge_last_ms = now_ms;
    } else {
      const uint32_t dt = now_ms - _charge_last_ms;
      _charge_last_ms = now_ms;
      // Защита от скачков времени и от долгих пауз: не больше минуты за тик.
      if (dt > 0 && dt <= 60000) {
        _charge_mah += i_abs * ((float)dt / 3600000.0f);
      }
      if (_charge_mah > _capacity_mah) _charge_mah = _capacity_mah;
    }
    _soc = _capacity_mah > 0.0f ? _charge_mah * 100.0f / _capacity_mah : _soc;
  }

  // --- кривая ---------------------------------------------------------------

  // Напряжение для заданного SOC: форма кривой, растянутая на окно химии.
  static uint16_t voltageForSoc(float soc, uint16_t v_full) {
    if (soc >= 100.0f) return v_full;
    if (soc <= 0.0f) return V_EMPTY_MV;

    const int n = SOC_CURVE_POINTS;
    float shape = (float)SOC_CURVE_SHAPE[n - 1].mv;

    // Таблица идёт по убыванию SOC, то есть по возрастанию напряжения.
    for (int i = 0; i < n - 1; i++) {
      float s_hi = (float)SOC_CURVE_SHAPE[i].soc;
      float s_lo = (float)SOC_CURVE_SHAPE[i + 1].soc;
      if (soc <= s_hi && soc >= s_lo) {
        float t = (soc - s_lo) / (s_hi - s_lo);
        shape = (float)SOC_CURVE_SHAPE[i + 1].mv
              + t * ((float)SOC_CURVE_SHAPE[i].mv - (float)SOC_CURVE_SHAPE[i + 1].mv);
        break;
      }
    }

    float frac = (shape - (float)SOC_CURVE_SHAPE_LO_MV)
               / ((float)SOC_CURVE_SHAPE_HI_MV - (float)SOC_CURVE_SHAPE_LO_MV);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    return (uint16_t)((float)V_EMPTY_MV + frac * (float)(v_full - V_EMPTY_MV) + 0.5f);
  }

  // Обратная задача: SOC по измеренному напряжению покоя.
  static float socFromVoltage(uint16_t mv) {
    const uint16_t v_full = vFullMv();
    if (mv >= v_full) return 100.0f;
    if (mv <= V_EMPTY_MV) return 0.0f;

    float frac = (float)(mv - V_EMPTY_MV) / (float)(v_full - V_EMPTY_MV);
    float shape = (float)SOC_CURVE_SHAPE_LO_MV
                + frac * ((float)SOC_CURVE_SHAPE_HI_MV - (float)SOC_CURVE_SHAPE_LO_MV);

    const int n = SOC_CURVE_POINTS;
    for (int i = 0; i < n - 1; i++) {
      float v_hi = (float)SOC_CURVE_SHAPE[i].mv;
      float v_lo = (float)SOC_CURVE_SHAPE[i + 1].mv;
      if (shape <= v_hi && shape >= v_lo) {
        float t = (shape - v_lo) / (v_hi - v_lo);
        return (float)SOC_CURVE_SHAPE[i + 1].soc
             + t * ((float)SOC_CURVE_SHAPE[i].soc - (float)SOC_CURVE_SHAPE[i + 1].soc);
      }
    }
    return (shape > (float)SOC_CURVE_SHAPE[0].mv) ? 100.0f : 0.0f;
  }

  // --- токи -----------------------------------------------------------------

  // Таблица токов передатчика для выбранного модуля.
  const uint16_t (*txTable() const)[POWER_DBM_COUNT] {
    switch (_module) {
      case MODULE_E22_900M30S:  return E22_900M30S_TX_MA;
      case MODULE_HT_RA62:      return HT_RA62_TX_MA;
      case MODULE_E22P_868M30S: return E22P_868M30S_TX_MA;
      // LR2021 (1 Вт): 22 dBm замерен, остальные мощности — расчёт от 33S.
      case MODULE_LR2021_1W:    return LR2021_1W_TX_MA;
      default:                  return E22_900M33S_TX_MA;
    }
  }

  float idleCurrentAt(uint16_t mv) const {
    // Строка по состоянию экрана: выключен / заставка / горит.
    const uint16_t *off, *saver, *on;
    switch (_module) {
      case MODULE_E22_900M30S:
        off = E22_900M30S_IDLE_OFF_MA; saver = E22_900M30S_IDLE_SAVER_MA; on = E22_900M30S_IDLE_ON_MA;
        break;
      case MODULE_HT_RA62:
        off = HT_RA62_IDLE_OFF_MA; saver = HT_RA62_IDLE_SAVER_MA; on = HT_RA62_IDLE_ON_MA;
        break;
      case MODULE_E22P_868M30S:
        off = E22P_868M30S_IDLE_OFF_MA; saver = E22P_868M30S_IDLE_SAVER_MA; on = E22P_868M30S_IDLE_ON_MA;
        break;
      case MODULE_LR2021_1W:
        off = LR2021_1W_IDLE_OFF_MA; saver = LR2021_1W_IDLE_SAVER_MA; on = LR2021_1W_IDLE_ON_MA;
        break;
      default:
        off = E22_900M33S_IDLE_OFF_MA; saver = E22_900M33S_IDLE_SAVER_MA; on = E22_900M33S_IDLE_ON_MA;
        break;
    }
    const uint16_t *row = (_display_state == 2) ? on : (_display_state == 1 ? saver : off);
    return interpByIdle(mv, row);
  }

  float txCurrentAt(uint16_t mv) const {
    const uint16_t (*tbl)[POWER_DBM_COUNT] = txTable();

    // Столбцы идут по возрастанию мощности: POWER_DBM = { -9, 0, 5, 10, 22 }.
    int c_lo = 0, c_hi = 0;
    for (int k = 0; k < POWER_DBM_COUNT; k++) {
      if ((int)_tx_dbm >= (int)POWER_DBM[k]) c_lo = k;
      if ((int)_tx_dbm <= (int)POWER_DBM[k]) { c_hi = k; break; }
    }

    float a = interpByVoltage(tbl, mv, c_lo);
    if (c_hi == c_lo) return a;
    float b = interpByVoltage(tbl, mv, c_hi);

    float p_lo = (float)POWER_DBM[c_lo];
    float p_hi = (float)POWER_DBM[c_hi];
    float t = ((float)_tx_dbm - p_lo) / (p_hi - p_lo);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return a + t * (b - a);
  }

  // Интерполяция по напряжению внутри одного столбца мощности.
  static float interpByVoltage(const uint16_t (*tbl)[POWER_DBM_COUNT], uint16_t mv, int col) {
    if (mv >= MEASURED_MV[0]) return (float)tbl[0][col];
    if (mv <= MEASURED_MV[MEASURED_MV_COUNT - 1]) {
      return (float)tbl[MEASURED_MV_COUNT - 1][col];
    }
    for (int r = 0; r < MEASURED_MV_COUNT - 1; r++) {
      uint16_t v_hi = MEASURED_MV[r];
      uint16_t v_lo = MEASURED_MV[r + 1];
      if (mv <= v_hi && mv >= v_lo) {
        float t = (float)(v_hi - mv) / (float)(v_hi - v_lo);
        float a = (float)tbl[r][col];
        float b = (float)tbl[r + 1][col];
        return a + t * (b - a);
      }
    }
    return (float)tbl[0][col];
  }

  // Интерполяция по напряжению внутри массива токов покоя.
  static float interpByIdle(uint16_t mv, const uint16_t *row) {
    if (mv >= MEASURED_MV[0]) return (float)row[0];
    if (mv <= MEASURED_MV[MEASURED_MV_COUNT - 1]) {
      return (float)row[MEASURED_MV_COUNT - 1];
    }
    for (int r = 0; r < MEASURED_MV_COUNT - 1; r++) {
      uint16_t v_hi = MEASURED_MV[r];
      uint16_t v_lo = MEASURED_MV[r + 1];
      if (mv <= v_hi && mv >= v_lo) {
        float t = (float)(v_hi - mv) / (float)(v_hi - v_lo);
        return (float)row[r] + t * ((float)row[r + 1] - (float)row[r]);
      }
    }
    return (float)row[0];
  }
};

}  // namespace darktec

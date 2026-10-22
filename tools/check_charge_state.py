#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Проверка определения заряда в модели питания DarkTec.

Повторяет логику DarktecPowerModel на Python и прогоняет ситуации, на которых
экран BATT врёт:

  A. нода включилась, когда зарядка уже стояла, потом зарядку вынули;
  B. нода работала от батареи, зарядку вставили, потом вынули.

Печатает то, что видит экран BATT: строку ETA и строку Inow. Старая логика
определяла заряд по знаку тока, который она угадывала один раз при старте;
новая берёт «зарядник подключён» с шины CHRG и смотрит только на модуль тока.

Запуск:  python tools/check_charge_state.py
"""

import sys

CHARGE_DETECT_MA = 20.0
CHARGE_TAIL_MA = 150.0
CHARGE_ETA_MIN_MA = 150.0
OCV_QUIET_MS = 3000
SOC_SMOOTH_ALPHA = 0.05
ETA_SMOOTH_ALPHA = 0.02
ETA_MAX_STEP_FRACTION = 0.01
IR_R_OHM = 0.2448
IR_K = 1.05
V_EMPTY_MV = 3000
V_FULL_MV = 4200

CAPACITY_MAH = 3000.0
TICK_MS = 250

# Токи покоя E22-900M33S по напряжению (экран выключен).
IDLE_MV = [4200, 3700, 3000]
IDLE_MA = [26.0, 33.0, 44.0]


def soc_from_mv(mv):
    if mv >= V_FULL_MV:
        return 100.0
    if mv <= V_EMPTY_MV:
        return 0.0
    return (mv - V_EMPTY_MV) * 100.0 / (V_FULL_MV - V_EMPTY_MV)


def mv_from_soc(soc):
    return V_EMPTY_MV + (V_FULL_MV - V_EMPTY_MV) * soc / 100.0


def idle_ma(mv):
    if mv >= IDLE_MV[0]:
        return IDLE_MA[0]
    for i in range(len(IDLE_MV) - 1):
        hi, lo = IDLE_MV[i], IDLE_MV[i + 1]
        if lo <= mv <= hi:
            t = (hi - mv) / (hi - lo)
            return IDLE_MA[i] + t * (IDLE_MA[i + 1] - IDLE_MA[i])
    return IDLE_MA[-1]


def ocv_from_terminal(mv, current_ma):
    v = mv - current_ma / 1000.0 * IR_R_OHM * IR_K
    return max(0.0, v)


class ModelOld:
    """DarktecPowerModel до правки: заряд по знаку тока, знак угадывается."""

    def __init__(self):
        self.capacity = CAPACITY_MAH
        self.soc = 100.0
        self.soc_valid = False
        self.charging = False
        self.invert = False
        self.polarity_known = False
        self.batt_ma = 0.0
        self.ocv_mv = 0.0
        self.track = False
        self.charge_mah = 0.0
        self.charge_start_mah = 0.0
        self.charge_start_ms = 0
        self.charge_last_ms = 0
        self.last_active_ms = 0
        self.eta_smooth = 0.0
        self.eta_valid = False
        self.charge_eta_smooth = 0.0
        self.charge_eta_valid = False

    def signed(self, raw):
        return -raw if self.invert else raw

    def learn_polarity(self, mv, raw):
        if self.polarity_known:
            return
        if abs(raw) < CHARGE_DETECT_MA:
            return
        soc_raw = soc_from_mv(mv)
        soc_inv = soc_from_mv(ocv_from_terminal(mv, -raw))
        self.invert = soc_inv > soc_raw
        self.polarity_known = True

    def update(self, mv, raw, now, radio_active=False):
        if radio_active:
            self.last_active_ms = now
            return
        charging = self.signed(raw) > CHARGE_DETECT_MA
        if not charging:
            if self.last_active_ms and now - self.last_active_ms < OCV_QUIET_MS:
                return
        if mv < 1500:
            return
        self.learn_polarity(mv, raw)
        self.charging = charging
        self.batt_ma = raw
        self.ocv_mv = ocv_from_terminal(mv, self.signed(raw))
        soc_now = soc_from_mv(self.ocv_mv)
        if not self.soc_valid:
            self.soc = soc_now
            self.soc_valid = True
        if charging:
            i_ma = self.signed(raw)
            if not self.track:
                self.charge_mah = self.capacity * self.soc / 100.0
                self.charge_start_mah = self.charge_mah
                self.charge_start_ms = now
                self.track = True
                self.charge_last_ms = now
            else:
                dt = now - self.charge_last_ms
                self.charge_last_ms = now
                if 0 < dt <= 60000:
                    self.charge_mah += i_ma * dt / 3600000.0
                self.charge_mah = min(self.charge_mah, self.capacity)
            self.soc = self.charge_mah * 100.0 / self.capacity
        else:
            self.track = False
            self.soc += (soc_now - self.soc) * SOC_SMOOTH_ALPHA
        self.smooth_eta()
        self.smooth_charge_eta(now)

    def charge_eta_minutes(self, now):
        if not self.soc_valid or not self.charging or not self.track:
            return 0
        i_ma = self.signed(self.batt_ma)
        if i_ma < CHARGE_TAIL_MA:
            return 0
        remain = self.capacity - self.charge_mah
        if remain <= 0:
            return 0
        i_eta = max(i_ma, CHARGE_ETA_MIN_MA)
        return int(remain / i_eta * 60.0 + 0.5)

    def smooth_eta(self):
        raw = self.compute_eta()
        if not self.eta_valid:
            self.eta_smooth = raw
            self.eta_valid = True
            return
        step = (raw - self.eta_smooth) * ETA_SMOOTH_ALPHA
        max_step = self.eta_smooth * ETA_MAX_STEP_FRACTION + 1.0
        step = max(-max_step, min(max_step, step))
        self.eta_smooth = max(0.0, self.eta_smooth + step)

    def smooth_charge_eta(self, now):
        if not self.charging:
            self.charge_eta_smooth = 0.0
            self.charge_eta_valid = False
            return
        raw = float(self.charge_eta_minutes(now))
        if not self.charge_eta_valid:
            self.charge_eta_smooth = raw
            self.charge_eta_valid = True
            return
        step = (raw - self.charge_eta_smooth) * ETA_SMOOTH_ALPHA
        max_step = self.charge_eta_smooth * ETA_MAX_STEP_FRACTION + 1.0
        step = max(-max_step, min(max_step, step))
        self.charge_eta_smooth = max(0.0, self.charge_eta_smooth + step)

    def compute_eta(self):
        # Разряд до пустого на токе текущего состояния (передач нет).
        remain = self.capacity * self.soc / 100.0
        i_ma = max(1.0, idle_ma(self.ocv_mv or mv_from_soc(self.soc)))
        return remain / i_ma * 60.0

    def eta_minutes(self):
        return int(self.eta_smooth + 0.5) if self.eta_valid else 0

    def state_load_ma(self):
        return idle_ma(self.ocv_mv or mv_from_soc(self.soc))


class ModelNew:
    """Модель после правки: заряд = зарядник подключён И ток по модулю."""

    def __init__(self):
        self.capacity = CAPACITY_MAH
        self.soc = 100.0
        self.soc_valid = False
        self.charging = False
        self.current_known = False
        self.batt_ma = 0.0
        self.ocv_mv = 0.0
        self.track = False
        self.charge_mah = 0.0
        self.charge_start_mah = 0.0
        self.charge_start_ms = 0
        self.charge_last_ms = 0
        self.last_active_ms = 0
        self.eta_smooth = 0.0
        self.eta_valid = False
        self.charge_eta_smooth = 0.0
        self.charge_eta_valid = False

    def update(self, mv, raw, now, radio_active=False, present=False):
        self.batt_ma = raw
        self.current_known = True
        i_abs = abs(raw)
        # Защёлка: пока зарядник подключён, сессия заряда не закрывается от
        # того, что ток в хвосте упал ниже порога.
        charging = present and (i_abs > CHARGE_DETECT_MA or self.charging)
        self.charging = charging
        if radio_active:
            self.last_active_ms = now

        if charging:
            if not self.soc_valid and mv >= 1500:
                # Первый замер пришёл уже под зарядом: процент сеем по
                # напряжению с компенсацией падения, иначе стартуем с 100%.
                self.soc = soc_from_mv(ocv_from_terminal(mv, i_abs))
                self.soc_valid = True
            if not self.track:
                self.charge_mah = self.capacity * self.soc / 100.0
                self.charge_start_mah = self.charge_mah
                self.charge_start_ms = now
                self.track = True
                self.charge_last_ms = now
            else:
                dt = now - self.charge_last_ms
                self.charge_last_ms = now
                if 0 < dt <= 60000:
                    self.charge_mah += i_abs * dt / 3600000.0
                self.charge_mah = min(self.charge_mah, self.capacity)
            self.soc = self.charge_mah * 100.0 / self.capacity
            self.smooth_eta()
            self.smooth_charge_eta(now)
            return

        self.track = False
        self.smooth_charge_eta(now)
        self.smooth_eta()

        if radio_active:
            return
        if self.last_active_ms and now - self.last_active_ms < OCV_QUIET_MS:
            return
        if mv < 1500:
            return
        self.ocv_mv = ocv_from_terminal(mv, -i_abs)
        soc_now = soc_from_mv(self.ocv_mv)
        if not self.soc_valid:
            self.soc = soc_now
            self.soc_valid = True
        self.soc += (soc_now - self.soc) * SOC_SMOOTH_ALPHA

    def charge_eta_minutes(self):
        if not self.soc_valid or not self.charging or not self.track:
            return 0
        i_ma = abs(self.batt_ma)
        if i_ma < CHARGE_TAIL_MA:
            return 0
        remain = self.capacity - self.charge_mah
        if remain <= 0:
            return 0
        return int(remain / max(i_ma, CHARGE_ETA_MIN_MA) * 60.0 + 0.5)

    def smooth_eta(self):
        raw = self.compute_eta()
        if not self.eta_valid:
            self.eta_smooth = raw
            self.eta_valid = True
            return
        step = (raw - self.eta_smooth) * ETA_SMOOTH_ALPHA
        max_step = self.eta_smooth * ETA_MAX_STEP_FRACTION + 1.0
        step = max(-max_step, min(max_step, step))
        self.eta_smooth = max(0.0, self.eta_smooth + step)

    def smooth_charge_eta(self, now):
        if not self.charging:
            self.charge_eta_smooth = 0.0
            self.charge_eta_valid = False
            return
        raw = float(self.charge_eta_minutes())
        if not self.charge_eta_valid:
            self.charge_eta_smooth = raw
            self.charge_eta_valid = True
            return
        step = (raw - self.charge_eta_smooth) * ETA_SMOOTH_ALPHA
        max_step = self.charge_eta_smooth * ETA_MAX_STEP_FRACTION + 1.0
        step = max(-max_step, min(max_step, step))
        self.charge_eta_smooth = max(0.0, self.charge_eta_smooth + step)

    def compute_eta(self):
        remain = self.capacity * self.soc / 100.0
        i_ma = max(1.0, idle_ma(self.ocv_mv or mv_from_soc(self.soc)))
        return remain / i_ma * 60.0

    def eta_minutes(self):
        return int(self.eta_smooth + 0.5) if self.eta_valid else 0

    def inow_ma(self):
        return abs(self.batt_ma) if self.current_known else self.state_load_ma()

    def state_load_ma(self):
        return idle_ma(self.ocv_mv or mv_from_soc(self.soc))


def render(model, old, now):
    """Две нижние строки экрана BATT: ETA и Inow."""
    if model.charging:
        eta = model.charge_eta_minutes(now) if old else model.charge_eta_minutes()
        if eta > 0:
            line = "ETA    CHG %dh %dm" % (eta // 60, eta % 60)
        else:
            line = "ETA    CHG done"
        if old:
            inow = "Inow   +%umA" % int(abs(model.batt_ma) + 0.5)
        else:
            inow = "Inow   +%umA" % int(model.inow_ma() + 0.5)
    else:
        eta = model.eta_minutes()
        line = "ETA    %dh %dm" % (eta // 60, eta % 60)
        if old:
            inow = "Inow   %umA" % int(model.state_load_ma() + 0.5)
        else:
            inow = "Inow   %umA" % int(model.inow_ma() + 0.5)
    return line, inow


def run(scenario, old, positive_is_discharge=True):
    """Сценарий возвращает список (t, plugged) — по нему строим ток."""
    if old:
        model = ModelOld()
    else:
        model = ModelNew()
    plugged = False
    mv = mv_from_soc(50.0)
    out = []
    for step in range(0, 1201):
        now = step * TICK_MS
        if step == 0 and scenario == "A":
            plugged = True
        if step == 600 and scenario == "A":
            plugged = False
        if step == 300 and scenario == "B":
            plugged = True
        if step == 900 and scenario == "B":
            plugged = False

        # Физика: под зарядом 1.5 А, на разряде ~40 мА.
        if plugged:
            raw = -1500.0 if positive_is_discharge else 1500.0
            mv = min(4150.0, mv + 0.4)
        else:
            raw = 40.0 if positive_is_discharge else -40.0
            mv = max(3300.0, mv - 0.002)

        if old:
            model.update(mv, raw, now)
        else:
            model.update(mv, raw, now, False, plugged)

        if step % 40 == 0 or step in (599, 601, 899, 901):
            line, inow = render(model, old, now)
            out.append((now // 1000, plugged, line, inow, int(model.soc)))
    return out


def show(title, rows):
    print(title)
    print("   t,с  заряд  ETA                Inow            SOC")
    for t, plugged, line, inow, soc in rows:
        print("  %5d  %5s  %-18s %-14s %3d%%" % (t, "да" if plugged else "нет", line, inow, soc))
    print()


def main():
    for scenario, name in (("A", "A: включились на зарядке, потом вынули"),
                           ("B", "B: работали от батареи, вставили и вынули")):
        for old in (True, False):
            rows = run(scenario, old, True)
            show("%s — %s" % (name, "СТАРАЯ логика" if old else "новая логика"), rows)
    print("Если нода включилась на зарядке, старая логика запоминает знак тока")
    print("наоборот: разряд 40 мА она принимает за заряд и пишет «CHG done»,")
    print("а настоящий заряд 1.5 А — за разряд, и показывает модельные 33 мА.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

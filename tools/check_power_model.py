# -*- coding: utf-8 -*-
"""Проверка алгоритма ETA: то же, что делает прошивка, но на Python.

Повторяет логику DarktecPowerModel::etaMinutes() по сгенерированным таблицам
и сравнивает с наивным «остаток / средний ток». Если числа расходятся
в разы или дают абсурд — значит в прошивке ошибка в единицах.
"""
import re
from pathlib import Path

HDR = Path(__file__).resolve().parent.parent / "variants" / "darktec" / "DarktecPowerTables.h"
txt = HDR.read_text(encoding="utf-8")


def grab(name):
    m = re.search(name + r"\[\]\s*=\s*\{([^}]*)\}", txt)
    if m:
        return [float(x) for x in m.group(1).split(",") if x.strip()]
    m = re.search(name + r"\[[^\]]*\]\s*=\s*\{", txt)
    return None


SOC = [(int(a), int(b)) for a, b in re.findall(r"\{\s*(\d+),\s*(\d+)\s*\}", txt)]
POWER = [int(x) for x in re.search(r"POWER_DBM\[\]\s*=\s*\{([^}]*)\}", txt).group(1).split(",")]
MEAS = [int(x) for x in re.search(r"MEASURED_MV\[\]\s*=\s*\{([^}]*)\}", txt).group(1).split(",")]
V_EMPTY = int(re.search(r"V_EMPTY_MV\s*=\s*(\d+)", txt).group(1))
V_FULL = int(re.search(r"V_FULL_LIION_MV\s*=\s*(\d+)", txt).group(1))
SHAPE_HI = int(re.search(r"SOC_CURVE_SHAPE_HI_MV\s*=\s*(\d+)", txt).group(1))
SHAPE_LO = int(re.search(r"SOC_CURVE_SHAPE_LO_MV\s*=\s*(\d+)", txt).group(1))


def table_rows(prefix):
    """Читает TX-таблицу модуля: список строк по напряжениям."""
    m = re.search(prefix + r"_TX_MA\[\]\[\d+\]\s*=\s*\{(.*?)\n\};", txt, re.S)
    rows = []
    for line in m.group(1).splitlines():
        nums = re.findall(r"[\d.]+", line.split("//")[0])
        if len(nums) >= len(POWER):
            rows.append([float(x) for x in nums[:len(POWER)]])
    return rows


def idle_row(prefix, display_on):
    # display_on: 0 — экран выключен, 1 — заставка (контраст 1), 2 — экран горит.
    key = {0: "_IDLE_OFF_MA", 1: "_IDLE_SAVER_MA", 2: "_IDLE_ON_MA"}[int(display_on)]
    m = re.search(prefix + key + r"\[\]\s*=\s*\{([^}]*)\}", txt)
    return [float(x) for x in m.group(1).split(",")]


def v_for_soc(soc):
    if soc >= 100:
        return V_FULL
    if soc <= 0:
        return V_EMPTY
    shape = SHAPE_LO
    for (s1, v1), (s2, v2) in zip(SOC, SOC[1:]):
        if s2 <= soc <= s1:
            t = (soc - s2) / (s1 - s2)
            shape = v2 + t * (v1 - v2)
            break
    frac = (shape - SHAPE_LO) / (SHAPE_HI - SHAPE_LO)
    return V_EMPTY + frac * (V_FULL - V_EMPTY)


def soc_for_v(mv):
    if mv >= V_FULL:
        return 100.0
    if mv <= V_EMPTY:
        return 0.0
    frac = (mv - V_EMPTY) / (V_FULL - V_EMPTY)
    shape = SHAPE_LO + frac * (SHAPE_HI - SHAPE_LO)
    for (s1, v1), (s2, v2) in zip(SOC, SOC[1:]):
        if v2 <= shape <= v1:
            t = (shape - v2) / (v1 - v2)
            return s2 + t * (s1 - s2)
    return 100.0 if shape > SOC[0][1] else 0.0


def interp1(row, mv):
    if mv >= MEAS[0]:
        return row[0]
    if mv <= MEAS[-1]:
        return row[-1]
    for a, b in zip(MEAS, MEAS[1:]):
        if b <= mv <= a:
            t = (a - mv) / (a - b)
            i = MEAS.index(a)
            return row[i] + t * (row[i + 1] - row[i])
    return row[0]


def idle_ma(prefix, mv, display_on):
    return interp1(idle_row(prefix, display_on), mv)


def tx_ma(prefix, mv, dbm):
    rows = table_rows(prefix)
    cols = [POWER.index(p) for p in POWER]
    lo = max([i for i, p in enumerate(POWER) if p <= dbm], default=0)
    hi = min([i for i, p in enumerate(POWER) if p >= dbm], default=len(POWER) - 1)
    a = interp1([r[lo] for r in rows], mv)
    if hi == lo:
        return a
    b = interp1([r[hi] for r in rows], mv)
    t = (dbm - POWER[lo]) / (POWER[hi] - POWER[lo])
    return a + t * (b - a)


def eta_minutes(prefix, capacity, dbm, display_on, soc, tx_fraction=0.0):
    total = 0.0
    s = soc
    while s > 0.5:
        step = min(1.0, s)
        mv = v_for_soc(s - step / 2)
        i_idle = idle_ma(prefix, mv, display_on)
        i_tx = tx_ma(prefix, mv, dbm)
        i = i_idle + (i_tx - i_idle) * tx_fraction
        total += (capacity * step / 100.0) / max(i, 1.0) * 60.0
        s -= step
    return total


print(f"кривая: {len(SOC)} точек, окно {V_EMPTY}..{V_FULL} мВ")
print()
print("=== SOC из напряжения (проверка формы) ===")
for mv in (4200, 4000, 3900, 3800, 3700, 3600, 3550, 3500, 3400, 3200, 3000):
    print(f"  {mv} мВ -> {soc_for_v(mv):5.1f}%   обратно -> {v_for_soc(soc_for_v(mv)):4.0f} мВ")
print()
print("=== ETA: 4000 мАч, передач нет, три состояния экрана ===")
for prefix, label in (("E22_900M33S", "33S"), ("E22_900M30S", "30S"),
                      ("HT_RA62", "RA62"), ("E22P_868M30S", "E22P")):
    e_off   = eta_minutes(prefix, 4000, 22, 0, 100.0) / 60
    e_saver = eta_minutes(prefix, 4000, 22, 1, 100.0) / 60
    e_on    = eta_minutes(prefix, 4000, 22, 2, 100.0) / 60
    print(f"  {label:5} : выкл {e_off:6.1f} ч   заставка {e_saver:6.1f} ч   "
          f"экран {e_on:6.1f} ч")
print()
print("=== ETA: 4000 мАч, экран включён, передач нет ===")
for prefix, label in (("E22_900M33S", "33S"), ("E22_900M30S", "30S"),
                      ("HT_RA62", "RA62"), ("E22P_868M30S", "E22P")):
    for dbm in (22, 0):
        e = eta_minutes(prefix, 4000, dbm, 2, 100.0)
        naive_i = idle_ma(prefix, v_for_soc(50.0), 2)
        naive = 4000 / naive_i * 60
        print(f"  {label:5} {dbm:3} дБм : ETA {e/60:6.1f} ч   "
              f"(наивно {naive/60:6.1f} ч по току при 50%)")
print()
print("=== с передачей 1% времени (4000 мАч, 33S) ===")
for frac in (0.0, 0.005, 0.01, 0.05):
    e = eta_minutes("E22_900M33S", 4000, 22, 2, 100.0, frac)
    print(f"  скважность {frac*100:4.1f}% -> ETA {e/60:6.1f} ч")

#!/usr/bin/env python3
"""
Генератор таблиц питания для DarkTec.

Собирает из сырых данных два массива для прошивки:

  * DarktecPowerTables.h — таблица «кривая разряда» (SOC -> напряжение)
    и таблицы токов по модулям (напряжение x мощность).

Запуск из корня проекта:

    python tools/gen_darktec_power_tables.py

Откуда берутся данные:

  * кривая разряда — CSV с iMax B6 (elapsed_s,voltage_V,capacity_mAh);
  * токи — замеры амперметром на живом железе, вписаны ниже в TABLES.
    Меняешь замер — правишь здесь, пересобираешь скрипт, таблицы обновляются.

Правила агрегации (решение владельца, 16.10.2026):

  * E22-900M33S   — измерено напрямую;
  * E22P-868M30S  — измерено напрямую;
  * E22-900M30S   — токи TX = половина от 33S, токи покоя как у 33S;
  * HT-RA62       — токи покоя как у 33S, ток передачи линейно по dBm
                    (28 мА внизу, 130 мА на 22 dBm).
"""

import csv
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "variants" / "darktec" / "DarktecPowerTables.h"

# --------------------------------------------------------------------------
# Входные данные

# Кривая разряда (100 мА) и кривая заряда (2 А) с одного и того же прогона iMax.
CURVE_CSV = Path(
    r"C:\Users\DARK4\Desktop\imaxcharger-main\imax_app\logs\LiION_4000_0.1A_discharge_thin.csv"
)
CHARGE_CSV = Path(
    r"C:\Users\DARK4\Desktop\imaxcharger-main\imax_app\logs\LiION_4000_0.1A_charge_thin.csv"
)

# Компенсация падения напряжения на внутреннем сопротивлении при заряде.
# R получено сравнением зарядной и разрядной кривых при одинаковом SOC
# (среднее 0.2448 Ом), коэффициент 1.05 подобран так, чтобы ошибка SOC
# по всей зарядной кривой была минимальной: средняя +0.7%, разброс ±4%.
IR_R_OHM = 0.2448
IR_K = 1.05

# Опорные напряжения и точки кривой
SOC_STEP = 5                       # шаг таблицы кривой, %
V_EMPTY_MV = 3000                  # низ кривой = порог отключения ноды
V_FULL_LIION_MV = 4200             # верх для Li-ion 1S
V_FULL_LIFEPO4_MV = 3650           # верх для LiFePO4 1S
V_FULL_LTO_MV = 2500               # верх для LTO 1S

# Напряжения, на которых снимались токи
V_MEASURED = (4200, 3700, 3000)

# Мощности, на которых снимались токи, dBm
P_MEASURED = (-9, 0, 5, 10, 22)

# Ключи режима
IDLE_OFF, IDLE_ON = "idle_off", "idle_on"
TX_KEY = {p: f"tx_{p}" for p in P_MEASURED}

# Замеры: модуль -> режим -> (ток при 4.2, 3.7, 3.0 В), мА
TABLES = {
    # E22-900M33S — измерено владельцем 16.10.2026
    "E22_900M33S": {
        IDLE_OFF: (25.7, 37.0, 50.0),
        IDLE_ON:  (32.0, 43.0, 58.0),
        TX_KEY[22]: (1479.0, 1730.0, 2795.0),
        TX_KEY[10]: (1258.0, 1473.0, 2326.0),
        TX_KEY[5]:  (1236.0, 1394.0, 1928.0),
        TX_KEY[0]:  (1103.0, 1228.0, 1753.0),
        TX_KEY[-9]: (717.0, 829.0, 1088.0),
    },
    # E22P-868M30S — измерено владельцем 16.10.2026
    "E22P_868M30S": {
        IDLE_OFF: (44.0, 54.0, 65.0),
        IDLE_ON:  (50.0, 59.0, 71.0),
        TX_KEY[22]: (1023.0, 1383.0, 1515.0),
        TX_KEY[10]: (990.0, 1124.0, 1493.0),
        TX_KEY[5]:  (479.0, 590.0, 686.0),
        TX_KEY[0]:  (288.0, 332.0, 423.0),
        TX_KEY[-9]: (135.0, 163.0, 194.0),
    },
}

# E22-900M30S — расчёт по правилу владельца: половина токов TX от 33S.
TABLES["E22_900M30S"] = {}
for _k, _v in TABLES["E22_900M33S"].items():
    if _k.startswith("idle"):
        TABLES["E22_900M30S"][_k] = _v
    else:
        TABLES["E22_900M30S"][_k] = tuple(round(_x / 2.0, 1) for _x in _v)

# HT-RA62 — токи покоя как у 33S, передача линейно по dBm.
RA62_TX_LOW_MA = 28.0     # ниже 0 dBm усилитель не работает: платим только за тракт
RA62_TX_HIGH_MA = 130.0   # на 22 dBm
_lo_p, _hi_p = -9, 22
TABLES["HT_RA62"] = {
    IDLE_OFF: TABLES["E22_900M33S"][IDLE_OFF],
    IDLE_ON:  TABLES["E22_900M33S"][IDLE_ON],
}
for _p in P_MEASURED:
    _t = (_p - _lo_p) / (_hi_p - _lo_p)
    _ma = RA62_TX_LOW_MA + _t * (RA62_TX_HIGH_MA - RA62_TX_LOW_MA)
    TABLES["HT_RA62"][TX_KEY[_p]] = (round(_ma, 1),) * 3   # от напряжения не зависит

MODULE_ORDER = ["E22_900M33S", "E22_900M30S", "HT_RA62", "E22P_868M30S"]

MODULE_COMMENT = {
    "E22_900M33S": "E22-900M33S — замерено",
    "E22_900M30S": "E22-900M30S — РАСЧЁТ: токи TX = половина от 33S, экран как у 33S",
    "HT_RA62":     "HT-RA62 — РАСЧЁТ: экран как у 33S, TX линейно по dBm (28..130 мА)",
    "E22P_868M30S": "E22P-868M30S — замерено",
}


def read_curve(path: Path):
    """CSV -> список (напряжение_мВ, снято_мАч), по возрастанию напряжения."""
    pts = []
    with path.open(encoding="utf-8", errors="replace") as f:
        for row in csv.DictReader(f):
            try:
                v = float(row["voltage_V"])
                c = float(row["capacity_mAh"])
            except (KeyError, TypeError, ValueError):
                continue
            pts.append((v, c))
    if not pts:
        raise SystemExit(f"в {path} не найдено ни одной строки с напряжением и ёмкостью")
    pts.sort(key=lambda x: x[1])
    return pts


def build_soc_table(pts):
    """Кривая -> таблица (SOC%, напряжение_мВ) с шагом SOC_STEP по снятой ёмкости."""
    c0 = pts[0][1]
    total = pts[-1][1] - c0
    if total <= 0:
        raise SystemExit("в CSV нулевая снятая ёмкость")

    out = []
    soc = 100
    while soc >= 0:
        target = total * (100 - soc) / 100.0
        v = min(pts, key=lambda x: abs((x[1] - c0) - target))[0]
        out.append((soc, int(round(v * 1000.0))))
        soc -= SOC_STEP
    return out


def read_charge_curve(path: Path, step_pct=5):
    """Зарядная кривая -> [(SOC%, напряжение при ~2 А)].

    В CSV ёмкость набегает от 0 до полной, то есть SOC = залито / всего.
    """
    pts = []
    with path.open(encoding="utf-8", errors="replace") as f:
        for row in csv.DictReader(f):
            try:
                v = float(row["voltage_V"])
                c = float(row["capacity_mAh"])
            except (KeyError, TypeError, ValueError):
                continue
            pts.append((v, c))
    if not pts:
        raise SystemExit(f"в {path} не найдено строк для зарядной кривой")
    pts.sort(key=lambda x: x[1])
    total = pts[-1][1] - pts[0][1]
    if total <= 0:
        raise SystemExit("в зарядной кривой нулевая залитая ёмкость")

    out = []
    for soc in range(0, 101, step_pct):
        target = total * soc / 100.0
        v = min(pts, key=lambda x: abs((x[1] - pts[0][1]) - target))[0]
        out.append((soc, int(round(v * 1000.0))))
    return out


def soc_to_voltage(table, soc, v_empty, v_full):
    """Напряжение на заданном SOC: форма кривой, растянутая на окно химии.

    table отсортирована по убыванию SOC. Берём «сырое» напряжение для этого
    SOC из формы, переводим в долю окна формы и растягиваем на окно химии.
    Линейная растяжка форму кривой не меняет, только масштаб.
    """
    shape_hi = table[0][1]     # напряжение при 100% по форме
    shape_lo = table[-1][1]    # напряжение при 0% по форме

    pts = sorted(table, key=lambda x: x[0])          # по возрастанию SOC
    soc = max(0.0, min(100.0, float(soc)))
    shape = shape_lo
    for (s1, v1), (s2, v2) in zip(pts, pts[1:]):
        if s1 <= soc <= s2:
            t = (soc - s1) / float(s2 - s1)
            shape = v1 + t * (v2 - v1)
            break
    else:
        shape = pts[-1][1] if soc >= pts[-1][0] else pts[0][1]

    frac = (shape - shape_lo) / float(shape_hi - shape_lo)
    return int(round(v_empty + frac * (v_full - v_empty)))


def main():
    pts = read_curve(CURVE_CSV)
    curve = build_soc_table(pts)
    charge = read_charge_curve(CHARGE_CSV)
    shape_hi, shape_lo = curve[0][1], curve[-1][1]

    # --- проверка монотонности: SOC вверх -> напряжение вверх
    for (s1, v1), (s2, v2) in zip(curve, curve[1:]):
        if v2 > v1:
            raise SystemExit(
                f"кривая немонотонна: {s1}% -> {v1} мВ, {s2}% -> {v2} мВ. "
                "Проверь исходный CSV."
            )

    L = []
    L.append("// ВНИМАНИЕ: файл сгенерирован автоматически.")
    L.append("// Источник: tools/gen_darktec_power_tables.py")
    L.append("// Правь замеры там и пересобирай — руками этот файл не редактируется.")
    L.append("#pragma once")
    L.append("")
    L.append("#include <stdint.h>")
    L.append("")
    L.append("namespace darktec {")
    L.append("")
    L.append("// ---------------------------------------------------------------------------")
    L.append("// Кривая разряда Li-ion: форма снята с реальной банки на токе 100 мА.")
    L.append(f"// {len(curve)} точек с шагом {SOC_STEP}%. Значения — «сырая форма»,")
    L.append("// привязанная к 3000 мВ внизу; верх задаёт химия (см. battery_chemistry.h).")
    L.append("// НЕ линейная: в середине диапазона кривая почти горизонтальна.")
    L.append("struct SocPoint { uint8_t soc; uint16_t mv; };")
    L.append("static const SocPoint SOC_CURVE_SHAPE[] = {")
    for soc, mv in curve:
        L.append(f"  {{ {soc:3d}, {mv:4d} }},   // {mv/1000.0:.3f} В")
    L.append("};")
    L.append(f"static const uint16_t SOC_CURVE_SHAPE_HI_MV = {shape_hi};")
    L.append(f"static const uint16_t SOC_CURVE_SHAPE_LO_MV = {shape_lo};")
    L.append(f"static const int      SOC_CURVE_POINTS      = {len(curve)};")
    L.append("")
    L.append("// --- Заряд: напряжение при токе около 2 А, по SOC ---")
    L.append("// Снято с той же банки, что и разряд. Нужно, чтобы во время заряда")
    L.append("// не показывать 100% только потому, что клеммы поднялись до 4.2 В.")
    L.append("static const SocPoint CHARGE_CURVE_2A[] = {")
    for soc, mv in charge:
        L.append(f"  {{ {soc:3d}, {mv:4d} }},   // {mv/1000.0:.3f} В")
    L.append("};")
    L.append(f"static const int CHARGE_CURVE_POINTS = {len(charge)};")
    L.append("")
    L.append("// Компенсация падения на внутреннем сопротивлении: V_разомкн = V_изм - I*R.")
    L.append("// R измерено сравнением зарядной и разрядной кривых при одинаковом SOC.")
    L.append(f"static const float IR_R_OHM = {IR_R_OHM}f;")
    L.append(f"static const float IR_K     = {IR_K}f;")
    L.append("")
    L.append("// Низ окна: порог отключения ноды. Один для всех химий.")
    L.append(f"static const uint16_t V_EMPTY_MV        = {V_EMPTY_MV};")
    L.append("// Верх окна по химии пакета (1 банка).")
    L.append(f"static const uint16_t V_FULL_LIION_MV   = {V_FULL_LIION_MV};")
    L.append(f"static const uint16_t V_FULL_LIFEPO4_MV = {V_FULL_LIFEPO4_MV};")
    L.append(f"static const uint16_t V_FULL_LTO_MV     = {V_FULL_LTO_MV};")
    L.append("")
    L.append("// ---------------------------------------------------------------------------")
    L.append("// Ток, мА. Строки — напряжения, на которых снимались замеры,")
    L.append("// столбцы — выставленная мощность передатчика в dBm.")
    L.append("// Порядок мощностей задан массивом POWER_DBM.")
    L.append("static const int16_t POWER_DBM[] = { "
             + ", ".join(str(p) for p in P_MEASURED) + " };")
    L.append(f"static const int POWER_DBM_COUNT = {len(P_MEASURED)};")
    L.append("static const uint16_t MEASURED_MV[] = { "
             + ", ".join(str(v) for v in V_MEASURED) + " };")
    L.append(f"static const int MEASURED_MV_COUNT = {len(V_MEASURED)};")
    L.append("")

    for mod in MODULE_ORDER:
        tab = TABLES[mod]
        L.append(f"// {MODULE_COMMENT[mod]}")
        L.append(f"static const uint16_t {mod}_IDLE_OFF_MA[] = {{ "
                 + ", ".join(f"{x:.1f}" for x in tab[IDLE_OFF]) + " };")
        L.append(f"static const uint16_t {mod}_IDLE_ON_MA[]  = {{ "
                 + ", ".join(f"{x:.1f}" for x in tab[IDLE_ON]) + " };")
        L.append(f"// строки: мощности {list(P_MEASURED)}")
        L.append(f"static const uint16_t {mod}_TX_MA[][{len(P_MEASURED)}] = {{")
        for i, v in enumerate(V_MEASURED):
            row = ", ".join(f"{tab[TX_KEY[p]][i]:.1f}" for p in P_MEASURED)
            L.append(f"  {{ {row} }},   // {v} мВ")
        L.append("};")
        L.append("")

    L.append("}  // namespace darktec")
    L.append("")

    OUT.write_text("\n".join(L), encoding="utf-8")

    print(f"записано: {OUT}")
    print(f"  точек кривой: {len(curve)}  ({curve[0][1]} мВ .. {curve[-1][1]} мВ)")
    print(f"  модулей: {len(MODULE_ORDER)}, мощностей: {len(P_MEASURED)}, "
          f"напряжений: {len(V_MEASURED)}")
    print(f"  зарядная кривая (2 А): {len(charge)} точек, "
          f"{charge[0][1]} .. {charge[-1][1]} мВ")
    print()
    print("  проверка кривой (Li-ion 1S, окно 3000..4200 мВ):")
    for soc in (100, 80, 60, 50, 40, 30, 20, 10, 5, 0):
        print(f"    {soc:3d}% -> {soc_to_voltage(curve, soc, V_EMPTY_MV, V_FULL_LIION_MV)} мВ")


if __name__ == "__main__":
    main()

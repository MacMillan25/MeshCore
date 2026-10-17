#!/usr/bin/env python3
# Генератор таблиц SOC для Darktec (spec 0001, FR-20, D-3).
#
# Вход:
#   --liion-csv PATH   CSV замера разряда: elapsed_s,voltage_V,capacity_mAh[,current_A]
#                      (например, LiION_4000_0.1A_discharge_thin.csv — разряд ≈ OCV).
#   -o PATH            выходной заголовок (variants/darktec/DarktecSocTables.h).
#
# Выход: variants/darktec/DarktecSocTables.h — три таблицы «напряжение покоя
# ячейки (мВ) → SOC» на сетке 0..100% шаг 2 (индекс = SOC/2, 51 точка).
# Li-ion — из CSV; LiFePO4/LTO — опорные точки из открытых источников
# (spec 0001 §8, approximation) до появления собственных замеров.
#
# Новые замеры подмешиваются регенерацией: положить CSV, запустить скрипт,
# закоммитить обновлённый заголовок (в шапке — источник и версия формата).

import argparse
import csv
import sys

STEP_PCT = 2
POINTS = 101 // STEP_PCT + 1  # SOC 0..100 включительно → 51 точка
VERSION = 1
SOURCE_DATE = "2026-10"

# Опорные точки (SOC % → мВ покоя ячейки), approximation по открытым данным.
LIFEPO4_ANCHORS = [
    (0, 2500), (5, 3050), (10, 3150), (20, 3250), (30, 3276), (40, 3283),
    (50, 3290), (60, 3298), (70, 3305), (80, 3320), (90, 3350), (100, 3400),
]
LTO_ANCHORS = [
    (0, 1800), (5, 2150), (10, 2250), (20, 2290), (30, 2320), (40, 2340),
    (50, 2360), (60, 2380), (70, 2400), (80, 2420), (90, 2450), (100, 2500),
]


def load_discharge_csv(path):
    """(voltage_V, capacity_mAh) построчно; ёмкость монотонно растёт."""
    samples = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            v = float(row["voltage_V"])
            c = float(row["capacity_mAh"])
            samples.append((v, c))
    if len(samples) < 2:
        sys.exit(f"CSV {path}: слишком мало строк")
    samples.sort(key=lambda s: s[1])
    return samples


def liion_table_from_csv(samples):
    """SOC-сетка → мВ: SOC = 100 − discharge/total·100, V — интерполяция по ёмкости."""
    total = samples[-1][1]
    grid = []
    for i in range(POINTS):
        soc = i * STEP_PCT
        discharged = total * (100 - soc) / 100.0
        # линейная интерполяция V по ёмкости
        v = None
        for k in range(1, len(samples)):
            c0, c1 = samples[k - 1][1], samples[k][1]
            if c0 <= discharged <= c1:
                if c1 > c0:
                    t = (discharged - c0) / (c1 - c0)
                else:
                    t = 0.0
                v = samples[k - 1][0] + t * (samples[k][0] - samples[k - 1][0])
                break
        if v is None:
            v = samples[0][0] if discharged <= samples[0][1] else samples[-1][0]
        grid.append(int(round(v * 1000.0)))
    # монотонность: с ростом SOC напряжение не убывает
    for i in range(1, POINTS):
        if grid[i] < grid[i - 1]:
            grid[i] = grid[i - 1]
    return grid, int(round(total))


def anchors_to_grid(anchors):
    grid = []
    for i in range(POINTS):
        soc = i * STEP_PCT
        v = None
        for k in range(1, len(anchors)):
            s0, v0 = anchors[k - 1]
            s1, v1 = anchors[k]
            if s0 <= soc <= s1:
                t = 0.0 if s1 == s0 else (soc - s0) / (s1 - s0)
                v = v0 + t * (v1 - v0)
                break
        if v is None:
            v = anchors[0][1] if soc < anchors[0][0] else anchors[-1][1]
        grid.append(int(round(v)))
    for i in range(1, POINTS):
        if grid[i] < grid[i - 1]:
            grid[i] = grid[i - 1]
    return grid


def emit_header(path, liion, lifepo4, lto, liion_source, total_mah):
    def arr(name, values):
        rows = []
        for i in range(0, POINTS, 10):
            chunk = ", ".join(f"{v:4d}" for v in values[i:i + 10])
            rows.append("  " + chunk + ",")
        body = "\n".join(rows)
        return f"inline constexpr uint16_t {name}[DARKTEC_SOC_POINTS] = {{\n{body}\n}};"

    content = f"""#pragma once
// Сгенерировано tools/gen_darktec_soc_tables.py — НЕ править вручную.
// Формат v{VERSION}: SOC 0..100 %, шаг {STEP_PCT} % (индекс = SOC / {STEP_PCT}),
// значение — напряжение покоя ЯЧЕЙКИ в мВ. Интерполяция линейная.
// Li-ion ({total_mah} mAh отдачи): {liion_source}
// LiFePO4/LTO: approximation по открытым данным (spec 0001 §8) — до собственных замеров.

#define DARKTEC_SOC_VERSION {VERSION}
#define DARKTEC_SOC_POINTS {POINTS}
#define DARKTEC_SOC_STEP_PCT {STEP_PCT}

// clang-format off
namespace DarktecSocTables {{

{arr("kLiion", liion)}

{arr("kLifepo4", lifepo4)}

{arr("kLto", lto)}

}}  // namespace DarktecSocTables
// clang-format on
"""
    with open(path, "w") as f:
        f.write(content)
    print(f"Записано {path}: {POINTS} точек, Li-ion full={liion[POINTS-1]} мВ, empty={liion[0]} мВ")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--liion-csv", required=True)
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--liion-source",
                    default=f"замер разряда MacMillan25 {SOURCE_DATE} (Li-ion 4000 mAh, ~0.11 А)")
    args = ap.parse_args()

    samples = load_discharge_csv(args.liion_csv)
    liion, total_mah = liion_table_from_csv(samples)
    lifepo4 = anchors_to_grid(LIFEPO4_ANCHORS)
    lto = anchors_to_grid(LTO_ANCHORS)
    emit_header(args.output, liion, lifepo4, lto, args.liion_source, total_mah)


if __name__ == "__main__":
    main()

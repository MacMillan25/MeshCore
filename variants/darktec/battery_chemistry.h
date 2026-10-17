#pragma once

/*
 * Химия батареи Darktec → шкала % UI и (опционально) пороги защиты.
 *
 * Аппаратный лимит: напряжение пакета ≤ 5 В.
 *   Li-ion / LiFePO4 : только 1S
 *   LTO              : 1S или 2S (2S full = 5000 мВ)
 *
 * Режим защиты (platformio.ini):
 *   -D DARKTEC_BATT_PROTECT=DARKTEC_BATT_PROTECT_OFF  — без auto hard cutoff
 *   -D DARKTEC_BATT_PROTECT=DARKTEC_BATT_PROTECT_ADC  — bootlock + AUTO_SHUTDOWN + ADC wait
 *     (см. DarktecAdcPower.h; LPCOMP/SYSTEMOFF не используем)
 *
 * powerOff() в обоих режимах идёт в ADC-wait: SYSTEMOFF на Darktec не
 * поднимается от VBAT зарядника на плате.

 * Пример:
 *   -D BATTERY_CHEMISTRY=BATTERY_CHEM_LIFEPO4
 *   -D BATTERY_CELLS=1
 *   -D DARKTEC_BATT_PROTECT=DARKTEC_BATT_PROTECT_ADC
 */

#define BATTERY_CHEM_LIION    1
#define BATTERY_CHEM_LIFEPO4  2
#define BATTERY_CHEM_LTO      3

#define DARKTEC_BATT_PROTECT_OFF  0
#define DARKTEC_BATT_PROTECT_ADC  1

#ifndef BATTERY_CHEMISTRY
#define BATTERY_CHEMISTRY  BATTERY_CHEM_LIION
#endif

#ifndef BATTERY_CELLS
#define BATTERY_CELLS  1
#endif

#ifndef DARKTEC_BATT_PROTECT
#define DARKTEC_BATT_PROTECT  DARKTEC_BATT_PROTECT_ADC
#endif

#if BATTERY_CHEMISTRY != BATTERY_CHEM_LIION && \
    BATTERY_CHEMISTRY != BATTERY_CHEM_LIFEPO4 && \
    BATTERY_CHEMISTRY != BATTERY_CHEM_LTO
#error "BATTERY_CHEMISTRY должен быть BATTERY_CHEM_LIION, BATTERY_CHEM_LIFEPO4 или BATTERY_CHEM_LTO"
#endif

#if DARKTEC_BATT_PROTECT != DARKTEC_BATT_PROTECT_OFF && \
    DARKTEC_BATT_PROTECT != DARKTEC_BATT_PROTECT_ADC
#error "DARKTEC_BATT_PROTECT должен быть DARKTEC_BATT_PROTECT_OFF или DARKTEC_BATT_PROTECT_ADC"
#endif

#if BATTERY_CELLS < 1
#error "BATTERY_CELLS должен быть >= 1"
#endif

#if BATTERY_CHEMISTRY != BATTERY_CHEM_LTO && BATTERY_CELLS > 1
#error "BATTERY_CELLS>1 недопустим для Li-ion/LiFePO4 на Darktec: вход батареи ограничен 5 В, поддерживается только 1S. Укажите BATTERY_CELLS=1 либо BATTERY_CHEM_LTO с BATTERY_CELLS=1 или 2."
#endif

#if BATTERY_CHEMISTRY == BATTERY_CHEM_LTO && BATTERY_CELLS > 2
#error "BATTERY_CELLS>2 недопустим для LTO на Darktec: вход батареи ограничен 5 В (LTO 2S full = 5000 мВ). Укажите BATTERY_CELLS=1 или BATTERY_CELLS=2."
#endif

#if BATTERY_CHEMISTRY == BATTERY_CHEM_LIION
#define BATT_CELL_EMPTY_MV     3000
#define BATT_CELL_FULL_MV      4200
#define BATT_CELL_BOOTLOCK_MV  3200
#define BATT_CELL_CRITICAL_MV  3000
#define BATT_CELL_WAKE_MV      3600

#elif BATTERY_CHEMISTRY == BATTERY_CHEM_LIFEPO4
#define BATT_CELL_EMPTY_MV     2500
#define BATT_CELL_FULL_MV      3650
#define BATT_CELL_BOOTLOCK_MV  2700
#define BATT_CELL_CRITICAL_MV  2500
#define BATT_CELL_WAKE_MV      3100

#elif BATTERY_CHEMISTRY == BATTERY_CHEM_LTO
#define BATT_CELL_EMPTY_MV     1800
#define BATT_CELL_FULL_MV      2500
#define BATT_CELL_BOOTLOCK_MV  2000
#define BATT_CELL_CRITICAL_MV  1800
#define BATT_CELL_WAKE_MV      2200
#endif

#define BATT_PACK_EMPTY_MV     (BATT_CELL_EMPTY_MV    * BATTERY_CELLS)
#define BATT_PACK_FULL_MV      (BATT_CELL_FULL_MV     * BATTERY_CELLS)
#define BATT_PACK_BOOTLOCK_MV  (BATT_CELL_BOOTLOCK_MV * BATTERY_CELLS)
#define BATT_PACK_CRITICAL_MV  (BATT_CELL_CRITICAL_MV * BATTERY_CELLS)
#define BATT_PACK_WAKE_MV      (BATT_CELL_WAKE_MV     * BATTERY_CELLS)

#if BATT_PACK_FULL_MV > 5000
#error "Полное напряжение пакета превышает лимит входа батареи Darktec 5 В"
#endif

// ---- Ёмкость и зарядный ток (spec 0001: FR-1, FR-31) ----
// Ёмкость ЯЧЕЙКИ: дефолт файла настроек /darktec.prefs при первом старте.
#ifndef BATTERY_CAPACITY_MAH
#define BATTERY_CAPACITY_MAH 4000
#endif
#if BATTERY_CAPACITY_MAH < 500 || BATTERY_CAPACITY_MAH > 30000
#error "BATTERY_CAPACITY_MAH должен быть в диапазоне 500..30000 mAh (большие LTO/LiFePO4)"
#endif

// Номинал зарядного тока платы: TPS54302 + резисторный делитель (FR-31).
// Прошивка ток не управляет; значение — для ETA (фаза 2) и детекции (FR-34).
#ifndef DARKTEC_CHARGE_CURRENT_MA
#define DARKTEC_CHARGE_CURRENT_MA 2000
#endif
#if DARKTEC_CHARGE_CURRENT_MA < 100 || DARKTEC_CHARGE_CURRENT_MA > 3000
#error "DARKTEC_CHARGE_CURRENT_MA должен быть в диапазоне 100..3000 mA"
#endif

#ifndef BATT_MIN_MILLIVOLTS
#define BATT_MIN_MILLIVOLTS  BATT_PACK_EMPTY_MV
#endif
#ifndef BATT_MAX_MILLIVOLTS
#define BATT_MAX_MILLIVOLTS  BATT_PACK_FULL_MV
#endif

#if DARKTEC_BATT_PROTECT == DARKTEC_BATT_PROTECT_ADC

#ifndef PWRMGT_VOLTAGE_BOOTLOCK
#define PWRMGT_VOLTAGE_BOOTLOCK  BATT_PACK_BOOTLOCK_MV
#endif
#ifndef PWRMGT_VOLTAGE_CRITICAL
#define PWRMGT_VOLTAGE_CRITICAL  BATT_PACK_CRITICAL_MV
#endif
#ifndef PWRMGT_VOLTAGE_WAKE
#define PWRMGT_VOLTAGE_WAKE  BATT_PACK_WAKE_MV
#endif
#ifndef AUTO_SHUTDOWN_MILLIVOLTS
#define AUTO_SHUTDOWN_MILLIVOLTS  BATT_PACK_CRITICAL_MV
#endif

#else  /* DARKTEC_BATT_PROTECT_OFF — без hard cutoff */

#ifndef PWRMGT_VOLTAGE_BOOTLOCK
#define PWRMGT_VOLTAGE_BOOTLOCK  0
#endif
#ifndef PWRMGT_VOLTAGE_CRITICAL
#define PWRMGT_VOLTAGE_CRITICAL  BATT_PACK_CRITICAL_MV
#endif
#ifndef PWRMGT_VOLTAGE_WAKE
#define PWRMGT_VOLTAGE_WAKE  BATT_PACK_WAKE_MV
#endif
/* AUTO_SHUTDOWN_MILLIVOLTS не задаём — companion не уходит в auto powerOff */

#endif

/* LPCOMP на Darktec не используем (brick при recovery с boost). */
#ifndef PWRMGT_LPCOMP_LOW_REFSEL
#define PWRMGT_LPCOMP_LOW_REFSEL  0
#endif
#ifndef PWRMGT_LPCOMP_REFSEL
#define PWRMGT_LPCOMP_REFSEL  0
#endif

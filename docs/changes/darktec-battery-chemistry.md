# Darktec — химия батареи, защита питания и радио

Форк аппаратного профиля ProMicro nRF52840 (`variants/darktec`) с выбором
химии батареи. Код ядра MeshCore (`src/helpers/NRF52Board.cpp` и т.п.)
**не изменяется** — только `variants/darktec/` (+ changelog здесь).

## Делитель батареи / АЦП

Делитель: **100к / 100к** (верхнее / нижнее плечо, коэффициент 2.0) на `D17` → AIN7.

`ADC_MULTIPLIER = 1.750f` в `DarktecBoard.h` — калибровка по мультиметру.

Питание MCU: **батарея → внешний DC-DC buck-boost → 3V3**.  
АЦП смотрит на тот же пакет через делитель. USB VBUS MCU к питанию, зарядке
и recovery **не используется**.

Зарядка пакета — своим зарядником на плате (не через USB ProMicro).

## Режим защиты: `DARKTEC_BATT_PROTECT`

В `variants/darktec/platformio.ini` (секция `[Darktec]`):

```ini
; Без auto cutoff (только шкала % по химии)
-D DARKTEC_BATT_PROTECT=DARKTEC_BATT_PROTECT_OFF

; Sleep/wake по АЦП (по умолчанию)
-D DARKTEC_BATT_PROTECT=DARKTEC_BATT_PROTECT_ADC
```

| Режим | Поведение |
|-------|-----------|
| `DARKTEC_BATT_PROTECT_OFF` | Нет boot-lock по химии. V-Cut из prefs и `initiateShutdown(LOW_VOLTAGE/BOOT_PROTECT)` идут в ADC-wait: нода остаётся в System ON и ждёт `V-Cut + 0.2 В`. `powerOff()` — отдельная гибернация через WFE, поднимается по RST. |
| `DARKTEC_BATT_PROTECT_ADC` | Hard cutoff: boot-lock + AUTO_SHUTDOWN + ADC-wait. Пробуждение по `VBAT ≥ V-Cut + 0.2 В` на делителе. |

LPCOMP на Darktec **не используется** (`PWRMGT_LPCOMP_*=0`): SYSTEMOFF+LPCOMP не поднимает плату от VBAT через buck-boost. SYSTEMOFF на этой плате вообще не используется: на nice!nano/ProMicro пин P0.13 (`SX126X_POWER_EN`) — это включатель 3.3 В, от которого питается и сам nRF; погасил — и обратно рейл не поднимается.

### Режим ADC — цикл (`DarktecAdcPower.h`)

1. Радио/экран off (`shutdownPeripherals()`); **3.3 В (P0.13) остаётся HIGH**
2. **Отключение SoftDevice** (иначе BLE держит мА → brownout → опрос АЦП прекращается)
3. Sleep ~1 с, замер АЦП делителя (нужны 3 стабильных чтения ≥ порога)
4. Если `mv >= V-Cut + 0.2 В` (`DARKTEC_LOW_BATT_WAKE_MARGIN_MV`) → `NVIC_SystemReset()`
5. Иначе снова sleep

Срабатывает при:

- boot, если `mv < PWRMGT_VOLTAGE_BOOTLOCK` (`DARKTEC_BATT_PROTECT_ADC`)
- `initiateShutdown(LOW_VOLTAGE|BOOT_PROTECT)` (оба режима)
- `powerOff()` — это отдельная гибернация через WFE, не ADC-wait

## Химия батареи

```ini
-D BATTERY_CHEMISTRY=BATTERY_CHEM_LIION
-D BATTERY_CELLS=1
-D TELEM_INA3221_SHUNT_VALUE=0.050
```

| Химия | empty/critical | bootlock | wake (ADC: V-Cut + 0.2) | UI full |
|-------|----------------|----------|-------------------------|---------|
| Li-ion 1S | 3000 | 3200 | 3200 | 4200 |
| LiFePO4 1S | 2500 | 2700 | 2700 | 3650 |
| LTO 1S | 1800 | 2000 | 2000 | 2500 |
| LTO 2S | ×2 | ×2 | 3800 | ×2 |

`wake` — для ADC-режима, где штатный V-Cut равен `critical`. В OFF-сборке
штатный V-Cut 3400 мВ, поэтому возврат 3600 мВ; если в prefs задан свой
V-Cut, возврат всегда `V-Cut + 200 мВ` (`DARKTEC_LOW_BATT_WAKE_MARGIN_MV`).
Пороги bootlock/critical применяются только при `DARKTEC_BATT_PROTECT_ADC`.
`BATT_MIN/MAX` (шкала %) — всегда.

## Радио по умолчанию

Файл `variants/darktec/radio_defaults.h` (подключается из `variant.h`).
Для **новых** prefs (первый старт / erase filesystem):

| Параметр | Значение |
|----------|----------|
| Частота | 869.075 МГц |
| Полоса | 62.5 кГц |
| SF | 8 |
| CR | 8 |
| TX power | 22 дБм |

`path.hash.mode` (2 байта / 32 хопа) — поле prefs приложений; без правок
`examples/` задаётся CLI: `set path.hash.mode 1`.

Уже сохранённые prefs на устройстве этими значениями не перезаписываются.

## Окружения сборки

`Darktec_repeater`, `Darktec_companion_radio_ble`, и остальные аналоги ProMicro.

CI (`.github/workflows/build-darktec-firmwares.yml` + `scripts/build-darktec-matrix.sh`)
на push в `south_edition` собирает обе защиты для каждого
роль×химия×ячейки варианта и публикует релиз:

`Darktec_{role}_{chem}_{cells}s_{adc|off}.{uf2,zip}`

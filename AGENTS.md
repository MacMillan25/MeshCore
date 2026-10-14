# AGENTS.md

## Communication preferences

- Always communicate with the repository owner in **Russian**, even when they write in English. Do not switch to another language unless they explicitly ask for it in a given request.

## Git-синхронизация: владелец не пользуется GitHub

Владелец репозитория (MacMillan25) **не умеет и не хочет работать с GitHub** — ни через сайт, ни через командную строку. Он работает с ИИ-ассистентом на **двух компьютерах** и ожидает, что **весь Git берёт на себя ассистент**:

- **«сохрани» / «залей» / «закинь на гит»** — закоммить изменения и сделай `git push` в `origin` сам, без вопросов «а какую ветку?».
- **Основное правило, чтобы две машины не разъезжались:** закончил работу над задачей — **сам закоммить и сам запушь** в `origin`, не дожидаясь просьбы. Владелец не должен помнить о Git вообще.
- **В начале каждой сессии** сделай `git fetch --all --prune` и подтяни актуальное состояние, прежде чем что-то править, — возможно, на второй машине уже есть свежие коммиты.
- **«всё с Гита» / «обнови» / «скачай»** — сделай `git fetch --all --prune`, покажи, что нового, и переключи/подтяни так, чтобы рабочая папка стала актуальной; при необходимости сообщи, что изменилось.
- Перед переключением ветки или перезаписью файлов **проверь, нет ли незакоммиченных правок** — и никогда не теряй работу владельца молча.
- Работай с `origin` = <https://github.com/MacMillan25/MeshCore.git> (его форк). `upstream` (`beeline09/MeshCore`) — только для чтения, не пушить туда.
- Учётные данные GitHub уже сохранены в Windows Credential Manager (пользователь `MacMillan25`) — доступ на запись проверен, push работает. Ничего настраивать не нужно.
- Полезные ветки владельца: `feat/darktec-e22p` (актуальная на 13.10.2026), `dev-darktec`, `south_edition`. `main` — релизная ветка форка.
- Коммиты пиши на русском, в стиле истории репозитория: `fix(darktec): …`, `feat(darktec): …`, `chore: …`.
- Если работал с файлами — заканчивай сессию чистой рабочей папкой (`git status`), чтобы следующая машина не спотыкалась.

## НИКОГДА не синхронизируй папку проекта облаком и не копируй её между машинами

**Транспорт между двумя компьютерами — только Git.** Не Яндекс.Диск, не OneDrive, не Dropbox, не «скопирую папку на флешку», не «перетащу через проводник».

Почему это жёсткое правило, а не пожелание:

- 13.10.2026 Яндекс.Диск уничтожил 708 файлов и половину каталога `.git` (HEAD, config, index, `.idx` pack-файла).
- 14.10.2026 копирование папки поверх самой себя положило в `.git` тринадцать мусорных файлов с суффиксом «(2)» (`index (2)`, `config (2)`, ссылки веток). Git перестал читать репозиторий, часть объектов пропала, а `variants/darktec/platformio.ini` исчез совсем. Лечилось только клоном заново; заодно по проекту разъехалось 165 файлов-дублей «(2)».
- Облачный клиент переписывает текстовые файлы, меняя LF на CRLF, и иногда обрывает запись на середине. Git после этого показывает изменёнными сотни файлов, которых никто не трогал.

**Что делать вместо этого:**

- Передал работу — закоммить и запушь в `origin`. Забрал на другой машине — `git fetch --all --prune` и подтяни.
- Если владелец предлагает «положить проект в облако, чтобы синхронизировалось» — объясни, что для кода это вредно, и предложи Git.
- Если проект всё же окажется внутри синхронизируемой папки — скажи владельцу и предложи вынести его.

### Если `git status` вдруг показывает кучу изменённых файлов

Сначала **проверь, а не коммить**. Скорее всего это следы облачного клиента:

- сравни хеши: `git hash-object --no-filters -- <файл>` против `git rev-parse HEAD:<файл>`. Совпали — правок нет, файл просто переписан клиентом;
- если расходятся **только переводы строк** — чини через `git add --renormalize -- <файлы>`, а не перезаписью файлов;
- держи `core.autocrlf=false` в этом репозитории: с `true` рабочее дерево вечно выглядит грязным.

Никогда не делай `git clean -fdx` и подобную массовую зачистку, не убедившись, что там нет работы владельца.

### Отдельная ловушка этого окружения: пароль для git-пуша

`gh` в роли credential-helper здесь **зависает** — git запускает его и бесконечно ждёт ответа, без всяких сообщений. Поэтому:

- не полагайся на сохранённый helper при `git push` из ассистента: бери токен через `gh auth git-credential get` и подставляй его прямо в URL на одну команду;
- **никогда не оставляй токен в `git remote set-url`** — он запишется в `.git/config` открытым текстом. После такой команды сразу возвращай чистый URL и проверяй конфиг;
- из этого же следует: если владелец захочет пушить сам из консоли, у него может всё зависнуть. Ему — через GitHub Desktop, либо пусть попросит ассистента.

## Проект DarkTec: изменения однотипны для всех модулей

Владелец работает над железом **DarkTec** (nRF52840 + SX1262, модули E22 / E22P). Главное правило, которое он просил запомнить:

- **Любая правка, касающаяся модуля или железа, делается ОДИНАКОВО для всех ролей**: репитер, room server, компаньон (USB и BLE), сенсор, KISS-модем, terminal chat. Нельзя поправить только BLE-компаньон и забыть репитер.
- **И для обоих модулей**: E22 и E22P. В `variants/darktec/platformio.ini` у каждой роли пара env — базовый (E22) и с суффиксом `_e22p`, всего 16 env (8 ролей × 2 модуля).
- Блоки `build_flags` у пары `X` и `X_e22p` **продублированы вручную** и часто содержат комментарий «менять надо в обоих местах, если что-то поменяется». Правя один env — правь и парный, иначе роли разъедутся.
- Отличие E22P от E22 — **ровно один флаг**: `-D DARKTEC_RADIO_E22P=1` (секция `[Darktec_e22p_flags]`). Он меняет полярность управления RF-трактом в `DarktecBoard.h`: у E22P пин 6 модуля — это EN (общий enable PA+LNA, держится высоким вручную, RadioLib его не трогает), у E22 — RXEN (управляется RadioLib по таблице RF-ключа).

### Где что лежит в DarkTec

- `variants/darktec/DarktecBoard.h` / `.cpp` — пины радио и платы, инициализация, питание, зарядка, INA3221. Общий код для ВСЕХ ролей.
- `variants/darktec/variant.h` / `variant.cpp` — таблица `g_ADigitalPinMap`, перевод Arduino-номера пина в ногу nRF (`P0.xx` / `P1.xx`).
- `variants/darktec/target.cpp` — сборка узла под роль, `MomentaryButton`.
- `variants/darktec/platformio.ini` — секции `[Darktec]`, `[Darktec_e22]`, `[Darktec_e22p]`, `[Darktec_e22p_flags]` и 16 `[env:...]`.

### Ловушка с номерами пинов

На этой плате **легко перепутать два разных способа нумерации**, потому что в `platformio.ini` рядом соседствуют оба:

- `-D PIN_BOARD_SDA=8` и `-D PIN_USER_BTN=6` — **оба являются Arduino-номерами** и оба идут через `g_ADigitalPinMap`:
  - SDA: 8 → `P1.04` (сходится с `PIN_WIRE_SCL=7` → `P0.11`);
  - кнопка: 6 → `P1.00`.
- Проверяй `g_ADigitalPinMap` в `variant.cpp` перед любой правкой пинов. Актуальная карта:

| Нога nRF | Arduino-пин | Занято под |
|---|---|---|
| P1.00 | 6 | `PIN_USER_BTN` / `PIN_BUTTON1` |
| P0.11 | 7 | `PIN_BOARD_SCL` → `Wire.setPins()` |
| P1.04 | 8 | `PIN_BOARD_SDA` → `Wire.setPins()` |
| P0.24 | 5 | **T1** — `PIN_GPS_EN` |
| P0.08 | 0 | **T2** — гейт VT3 (в прошивке не используется) |
| P0.06 | 1 | **T3** — `PIN_BUZZER`, гейт VT4 |
| P0.17 | 2 | EN модуля E22P (у E22 — RXEN) |
| P0.13 | 21 | `SX126X_POWER_EN`, `PIN_EXT_VCC` |
| P0.31 | 17 | `PIN_VBAT_READ` / `PIN_BATTERY` |
| P0.03 / P0.04 | 3 / 4 | `PIN_GPS_RX` / `PIN_GPS_TX` |

### Железо DarkTec: транзисторы (подтверждено по схеме V2.1)

На плате **четыре** мосфета: три управляемых N-канальных `IRLML6344` (VT2, VT3, VT4) с резисторами 10 к на затворах (R30, R31, R32), плюс `VT1` — P-канальный `AO3401A` во входной цепи зарядника CN3795 (защита от переполюсовки, к трём «рабочим» отношения не имеет).

| Имя в прошивке | Схема | Нога MCU | Arduino-пин | Что коммутирует |
|---|---|---|---|---|
| **T1** | VT2 (IRLML6344) | P0.24 | 5 | GPS: `-D PIN_GPS_EN=5` |
| **T2** | VT3 (IRLML6344) | P0.08 | 0 | не задействован |
| **T3** | VT4 (IRLML6344) | P0.06 | 1 | **буззер**: `-D PIN_BUZZER=1` |

- В `platformio.ini` на схеме ноги подписаны как `T1/P0.24`, `T2/P0.08`, `T3/P0.06` — то есть «T1…T3» это шелкография платы, а `VT2…VT4` — позиционные обозначения.
- **T3 уже работает, писать ничего не нужно.** `-D PIN_BUZZER=1` задан во всех четырёх companion-env (`Darktec_companion_radio_usb`, `_usb_e22p`, `_ble`, `_ble_e22p`). Нога 1 через `g_ADigitalPinMap` = P0.06 = затвор VT4. Отдельного `PIN_BUZZER_EN` у DarkTec нет — включателем служит сам транзистор.
- **T1 занят под GPS** (`PIN_GPS_EN`), под другую задачу его без переделки не отдать.
- **Исправление прежнего вывода:** I2C дисплея **не имеет отношения** к P0.08. Реальная шина — `Wire.setPins(8, 7)` в `DarktecBoard.cpp`, то есть SDA = `P1.04`, SCL = `P0.11`. Подтверждено и схемой, и замером на устройстве. Определения `PIN_WIRE_SDA=6` / `PIN_WIRE_SCL=7` в `variant.h` (дают P1.00 и P0.11) — **устаревшие и в сборке не используются**, но могут сбить с толку.
- Прочая обвязка питания, полезная для контекста: делитель VBAT 100к/100к на P0.31 (в схеме помечен как вход LPCOMP), шунт 50 мОм, что сходится с `TELEM_INA3221_SHUNT_VALUE 0.050`.

### Цепочка буззера (тройное нажатие кнопки)

```
3× нажатие BTN
  -> UITask::handleTripleClick()      examples/companion_radio/ui-new/UITask.cpp:2590
  -> UITask::toggleBuzzer()                                          :2634
  -> buzzer.quiet(true/false)         src/helpers/ui/buzzer.cpp:47
  -> _node_prefs->buzzer_quiet + savePrefs()
  -> showAlert("Buzzer: OFF" / "Buzzer: ON")
```

Звук играет RTTTL по `PIN_BUZZER`; `quiet()` лишь запрещает запуск мелодий. Мелодии — в `src/helpers/ui/buzzer.h` (`startup_song`, `shutdown_song`).

**Осторожно с определениями UART в `variant.h`.** Там записано `PIN_SERIAL1_TX=1` и `PIN_SERIAL1_RX=0`, то есть устаревшая привязка UART к **тем же самым** ногам P0.06 (буззер) и P0.08 (T2). На реальной плате UART живёт на P0.20/P0.22 (`PIN_GPS_RX=3` / `PIN_GPS_TX=4`): при `ENV_INCLUDE_GPS=1` вызов `EnvironmentSensorManager::initBasicGPS()` делает `Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX)` и уводит его туда. Но маршрут `WITH_RS232_BRIDGE_RX/TX` в двух env по-прежнему ссылается на `PIN_SERIAL1_RX/TX`. **Если буззер вдруг замолчал или заквакал странно — первым делом проверяй, не поднялся ли Serial1 на P0.06.**

### Удалённо управляемые выходы: ключ RELAY (T2)

Реле на T2 управляется **штатным механизмом настроек сенсоров** — протокол, приложение и каналы не трогались вообще. Реализация целиком в `variants/darktec/DarktecSensorManager.h` (обёртка над `EnvironmentSensorManager`, общий код для всех 16 env).

Команды (регистр не важен):

```
sensor set RELAY ON | OFF | TOGGLE     ->  "RELAY: ON" / "RELAY: OFF"
sensor get RELAY                       ->  "> ON" / "> OFF"
sensor list                            ->  RELAY=... (и прочие настройки)
```

Принимаются также `1`/`0`, `true`/`false`, `yes`/`no`, синонимы ключа `t2`. **При старте реле всегда выключено** — так решил владелец (безопасно для силовой нагрузки). Состояние намеренно НЕ сохраняется в prefs.

Как команда доходит до ноды:

| Роль | Путь |
|---|---|
| Репитер | личка от админа → CLI (`simple_repeater/MyMesh.cpp:812`) |
| Комнатный сервер | личка/пост от админа → CLI (`simple_room_server/MyMesh.cpp:441,467`) |
| Сенсор | личка от админа → CLI (`simple_sensor/SensorMesh.cpp:561`) |
| Компаньон | **Remote CLI**: личка вида `<CLI-PIN> <команда>` (`companion_radio/MyMesh.cpp:835`) или канал `TerminalCLI` (`:1676`) |
| Любая роль от приложения | serial/BLE `CMD_SET_CUSTOM_VAR` в формате `RELAY:ON` (`companion_radio/MyMesh.cpp:2417`) |

**Remote CLI у компаньона** — основной путь для бытового сценария («нода дома, я с телефоном в городе»): PIN это 8 hex-символов от последних 4 байт **приватного** ключа (не выводится из публичного), смотреть командой `pin`, включается `set remote.cli on/off` (по умолчанию on). Ответ приходит личкой, длинный режется на чанки `[1/N]`.

**Важно про «приватные» каналы:** канал в MeshCore — это групповой канал на общем PSK, а не адресная переписка. У `TerminalCLI` PSK вообще фиксированный и публичный (`MyMesh.h:89`). Для секретных команд использовать Remote CLI по личке, а не каналы.

**Что НЕ сделано (осознанно):** ключ `GPSPWR` для T1 (питание GPS) зарезервирован, но возвращает `false`. Причина: `setSettingValue("gps", ...)` управляет только данными, а чипу GPS после подачи питания нужна пауза на старт — иначе детект GPS сломается. Делать это надо вместе с драйвером GPS, а не отдельным ключом.

**Грабли сборки:** `.pio` — каталог сборочный и в git не хранится, поэтому после переезда папки или клона заново его приходится наполнять заново. Если PlatformIO падает с `MissingPackageManifestError` или «библиотека не найдена» — удали `.pio/libdeps/<env>` и запусти `pio pkg install -e <env>`. Первая сборка после чистого клона качает тулчейн и библиотеки (~15 минут), дальше сборка одного env ~35 с. PlatformIO живёт в `%USERPROFILE%\.platformio\penv\Scripts\pio.exe` и в PATH его нет.

### Сборка прошивок и UF2: чего `pio run` НЕ делает

**`pio run` НЕ создаёт `.uf2`.** Он делает только `firmware.elf`, `firmware.hex` и `firmware.zip`. Причём `firmware.zip` — это пакет Nordic DFU, внутри лежат `firmware.bin`, `firmware.dat` и `manifest.json`; **никакого UF2 там нет**. Если владелец спрашивает «где uf2» — их надо создать отдельно, это не ошибка сборки.

Способы получить `.uf2`:

1. **Цель `create_uf2`** (её использует `build-darktec-uf2.ps1`): `pio run -e <env> -t create_uf2` → `.pio/build/<env>/firmware.uf2`.
2. **Напрямую `bin/uf2conv/uf2conv.py`** (обходит проблему с кириллицей, см. ниже):
   `python bin\uf2conv\uf2conv.py <.pio\...\firmware.hex> -c -o <.pio\...\firmware.uf2> -f 0xADA52840`

Проверка, что файл настоящий UF2: первые 4 байта `55 46 32 0a`, последние 4 байта блока `30 6f b1 0a`, familyID (байты 28–31) `40 28 a5 ad` = `0xADA52840`, размер кратен 512.

**Про кириллицу в пути.** На 13–14.10.2026 проект лежал в `…\YandexDisk\Компьютер ANDREY_PC\…`, и `create-uf2.py`, собирающий команду строкой через `env.Execute`, отдавал путь до питона искажённым (`��������� ANDREY_PC`) → `uf2conv.py: error: unrecognized arguments`, цель `create_uf2` падала. Сейчас проект перенесён в путь без кириллицы (`…\Desktop\Meshcore\MeshCore-git`), и цель `create_uf2` работает штатно. Если владелец снова положит проект в путь с кириллицей — грабля вернётся, тогда:

- `build-darktec-uf2.ps1` в этом случае печатает ошибку uf2conv, но **всё равно выкладывает файл** — он делает `Copy-Item` из `.pio\build\<env>\firmware.uf2`, а его создаёт PlatformIO своим внутренним питоном. Файл валидный, можно прошивать;
- либо собрать uf2 напрямую: `python bin\uf2conv\uf2conv.py <.pio\...\firmware.hex> -c -o <.pio\...\firmware.uf2> -f 0xADA52840`.

- Готовые UF2 удобно складывать в `out\uf2\` (каталог в `.gitignore`) с именем `<env>-<версия>-<дата>.uf2`.

**Запуск `.ps1` в этой системе:** `ExecutionPolicy = Restricted`, поэтому скрипты не запускаются напрямую. Обоход: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-darktec-uf2.ps1 -Env <env>` (именно `powershell.exe`, а не `pwsh` — на этой машине есть только Windows PowerShell 5.1).

## Cursor Cloud specific instructions

This repo is the **MeshCore `south_edition` firmware** — embedded C++ built with **PlatformIO** (Arduino framework). There are **no servers, databases, or long-running services**; the product is firmware binaries flashed onto LoRa radio boards. "Running the app" without physical hardware means (a) running the native host unit tests and (b) compiling a firmware target into a `.bin`/`.uf2`.

### Toolchain / how it runs
- The build engine is the PlatformIO CLI (`pio`), installed via `pip install --upgrade platformio` (see the update script). It lands in `~/.local/bin`, which has been added to `PATH` in `~/.bashrc`. If `pio` is not found in a fresh shell, run `export PATH="$HOME/.local/bin:$PATH"`.
- First `pio test`/`pio run` for a given platform downloads large toolchains/libraries (espressif32, nordicnrf52, etc.) into `~/.platformio` and needs network access. Subsequent builds use that cache. An ESP32-S3 first build takes ~2 min; incremental builds are seconds.

### Test / build / run (no hardware needed)
- Unit tests (host/native, GoogleTest): `pio test -e native -vv`. Only `src/Utils.cpp` is covered today (5 tests in `test/test_utils/`). Note the trailing PlatformIO summary prints `0 test cases: 0 succeeded` — that is a known cosmetic quirk; trust the `native:test_utils [PASSED]` line instead.
- List all build targets (environments): `sh build.sh list` or `pio project config`. There are 800+ `<board>_<app>` env combinations from `variants/` × `examples/`.
- Build one firmware target: `pio run -e <env>` (e.g. `pio run -e Heltec_v3_companion_radio_ble`). Output binary lands in `.pio/build/<env>/firmware.bin` (ESP32) or `firmware.uf2` (nRF52/RP2040). The `.github/workflows/pr-build-check.yml` matrix lists representative envs across each platform.
- Release-style build with versioning/packaging into `out/`: `FIRMWARE_VERSION=v0.0.0 sh build.sh build-firmware <env>`.
- Flashing (`pio run -e <env> -t upload`) and `pio device monitor` require physical hardware/USB and cannot run in this VM.

### Lint / formatting
- No CI lint job. A `.clang-format` exists but is **not** enforced; per README/CONTRIBUTING, do **not** retroactively reformat existing code (it creates noisy diffs).

### PR conventions
- Per README, the intended base branch for contributions is `dev`. Automated-agent PRs may append `🤖🤖` to the PR title to opt into fast-track merging (see `CONTRIBUTING.md`).

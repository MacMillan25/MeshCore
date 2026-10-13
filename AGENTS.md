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
- Работай с `origin` = <https://github.com/MacMillan25/MeshCore.git> (его форк). `upstream` (`meshcore-dev/MeshCore`) — только для чтения, не пушить туда.
- Учётные данные GitHub уже сохранены в Windows Credential Manager (пользователь `MacMillan25`) — доступ на запись проверен, push работает. Ничего настраивать не нужно.
- Полезные ветки владельца: `feat/darktec-e22p` (актуальная на 13.10.2026), `dev-darktec`, `south_edition`. `main` — релизная ветка форка.
- Коммиты пиши на русском, в стиле истории репозитория: `fix(darktec): …`, `feat(darktec): …`, `chore: …`.
- Если работал с файлами — заканчивай сессию чистой рабочей папкой (`git status`), чтобы следующая машина не спотыкалась.

### Ловушка этой рабочей папки: Яндекс.Диск

Репозиторий лежит **внутри синхронизируемой папки Яндекс.Диска** (`…\YandexDisk\Компьютер ANDREY_PC\Meshcore\MeshCore-git`). Из-за этого:

- Яндекс.Диск переписывает текстовые файлы, **меняя LF на CRLF**, и иногда обрывает запись на середине — 13.10.2026 это уже уничтожило 708 файлов и половину каталога `.git` (HEAD, config, index, `.idx` pack-файла).
- Если `git status` показывает изменёнными кучу файлов, которые ты не трогал — **сначала проверь хеши**, а не коммить: `git hash-object --no-filters -- <файл>` против `git rev-parse HEAD:<файл>`. Совпали — значит правок нет.
- Если расходятся **только переводы строк** — правь их через `git add --renormalize -- <файлы>`, а не перезаписью файлов.
- Никогда не делай `git clean -fdx` и подобную массовую зачистку, не убедившись, что там нет работы владельца.

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

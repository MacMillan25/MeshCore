<#
.SYNOPSIS
    Сборка прошивки Darktec и выкладка готового .uf2 в out\uf2.

.DESCRIPTION
    Две ветки по радиомодулю, у каждой роли ноды своя пара env:

        E22-900M30S   имя env без суффикса        метка версии "SE"
        E22P-868M30S  имя env с суффиксом _e22p   метка версии "E22P"

    Роли: companion_radio_ble, companion_radio_usb, repeater,
    repeater_bridge_rs232_serial1, room_server, terminal_chat, sensor, kiss_modem.

    Метку версии видит только UI компаньона — в остальных ролях своя отрисовка
    заставки, туда FIRMWARE_VERSION_SUFFIX ещё не добавлен.

    Метка задана в variants\darktec\platformio.ini, менять её здесь не нужно —
    см. пример с -VersionSuffix ниже.

    git ставить не нужно: версия берётся из examples\companion_radio\MyMesh.h,
    а git (нужен PlatformIO, чтобы скачать фреймворк nRF52) скрипт находит сам —
    берёт тот, что лежит внутри GitHub Desktop.

    Запуск из корня проекта (ветка E22, компаньон BLE):
        .\build-darktec-uf2.ps1

    Ветка E22P, компаньон BLE:
        .\build-darktec-uf2.ps1 -Env Darktec_companion_radio_ble_e22p

    Другая роль, обе ветки:
        .\build-darktec-uf2.ps1 -Env Darktec_repeater
        .\build-darktec-uf2.ps1 -Env Darktec_repeater_e22p

    Своё имя версии (попадёт и в прошивку, и в имя файла):
        .\build-darktec-uf2.ps1 -Version v1.17.1b7

    Явный суффикс:
        .\build-darktec-uf2.ps1 -VersionSuffix "SE-test"
#>

param(
    [string] $Env = 'Darktec_companion_radio_ble',
    [string] $Version,
    [string] $VersionSuffix,
    [string] $OutDir = 'out\uf2'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

function Get-Pio {
    $candidates = @(
        (Join-Path $env:USERPROFILE '.platformio\penv\Scripts\platformio.exe'),
        (Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe')
    )
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    $cmd = Get-Command pio -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "PlatformIO не найден. Искал в $($candidates -join ', ')"
}

# PlatformIO ставит фреймворк nRF52 через `git clone`, поэтому git обязан быть
# доступен как команда. Отдельно ставить не нужно — рабочий git лежит внутри
# GitHub Desktop. Если его нет в PATH, добавляем на время работы скрипта.
function Ensure-Git {
    if (Get-Command git -ErrorAction SilentlyContinue) { return }

    $found = $null
    $ghRoot = Join-Path $env:LOCALAPPDATA 'GitHubDesktop'
    if (Test-Path $ghRoot) {
        $found = Get-ChildItem $ghRoot -Directory -ErrorAction SilentlyContinue |
                 Sort-Object Name -Descending |
                 ForEach-Object { Join-Path $_.FullName 'resources\app\git\cmd\git.exe' } |
                 Where-Object { Test-Path $_ } |
                 Select-Object -First 1
    }
    if (-not $found) {
        foreach ($p in @("$env:ProgramFiles\Git\cmd\git.exe", "${env:ProgramFiles(x86)}\Git\cmd\git.exe")) {
            if (Test-Path $p) { $found = $p; break }
        }
    }
    if (-not $found) {
        throw "git не найден, а без него PlatformIO не скачает фреймворк nRF52. Поставь Git for Windows: https://git-scm.com/downloads"
    }

    # PATH берём ещё и из реестра: в текущем процессе окружение может быть устаревшим.
    $userPath = [Environment]::GetEnvironmentVariable('PATH', 'User')
    $env:PATH = "$(Split-Path $found);$userPath;$env:PATH"
    Write-Host "git:      $found"
}

# Версия по умолчанию — из MyMesh.h, как это делает scripts/darktec-version.sh.
function Get-BaseVersion {
    $meshH = Join-Path $root 'examples\companion_radio\MyMesh.h'
    $m = Select-String -Path $meshH -Pattern '#define\s+FIRMWARE_VERSION\s+"(v[0-9]+\.[0-9]+\.[0-9]+[0-9a-z]*)"' |
         Select-Object -First 1
    if ($m) { return $m.Matches[0].Groups[1].Value }
    throw "Не нашёл FIRMWARE_VERSION в $meshH"
}

$pio = Get-Pio
Ensure-Git
if (-not $Version) { $Version = Get-BaseVersion }

$fullVersion = $Version
if ($VersionSuffix) { $fullVersion = "$Version $VersionSuffix" }

Write-Host "Env:      $Env"
Write-Host "Version:  $fullVersion"
Write-Host "Pio:      $pio"
Write-Host ""

# Override версии — через отдельный -include заголовок, а НЕ через
# PLATFORMIO_BUILD_FLAGS: эта переменная ПЕРЕЗАПИСЫВАЕТ build_flags из ini,
# и вместе с ней потерялись бы все пины, UI_*_PAGE и NRF52_PLATFORM.
$includeArgs = @()
if ($PSBoundParameters.ContainsKey('Version') -or $VersionSuffix) {
    $defsHeader = Join-Path $root '.pio\darktec_local_defs.h'
    New-Item -ItemType Directory -Force -Path (Split-Path $defsHeader) | Out-Null
    @(
        '#pragma once',
        "#define FIRMWARE_VERSION `"$fullVersion`""
    ) | Set-Content -Path $defsHeader -Encoding ascii
    $includeArgs = @('-DPLATFORMIO_BUILD_FLAGS=-include ' + $defsHeader)
    Write-Host "Override: $defsHeader"
    Write-Host ""
}

Push-Location $root
try {
    # `pio run` для nRF52 делает .hex и .zip (Nordic DFU), а .uf2 собирается
    # отдельной целью create_uf2 из create-uf2.py — без неё файла не будет.
    $pioArgs = @('run', '-e', $Env, '-t', 'create_uf2') + $includeArgs
    & $pio @pioArgs
    if ($LASTEXITCODE -ne 0) { throw "Сборка $Env упала (код $LASTEXITCODE)" }

    $built = Join-Path $root ".pio\build\$Env\firmware.uf2"
    if (-not (Test-Path $built)) { throw "Сборка прошла, но нет файла $built" }

    $outPath = Join-Path $root $OutDir
    New-Item -ItemType Directory -Force -Path $outPath | Out-Null

    $stamp = Get-Date -Format 'yyyy-MM-dd'
    $slug  = ($fullVersion -replace '[^A-Za-z0-9.\-]', '-')
    $dest  = Join-Path $outPath "$Env-$slug-$stamp.uf2"

    Copy-Item $built $dest -Force

    $fi = Get-Item $dest
    Write-Host ""
    Write-Host "Готово: $($fi.FullName)"
    Write-Host ("Размер: {0:N0} байт ({1:N1} КБ)" -f $fi.Length, ($fi.Length / 1KB))
}
finally {
    Pop-Location
}

@echo off
rem Сборка прошивки Darktec (компаньон BLE) и выкладка .uf2 в out\uf2
rem Запускается двойным щелчком или из консоли:  build-darktec-uf2.cmd
rem
rem Обёртка нужна потому, что в системе запрещён запуск .ps1-скриптов
rem (Execution Policy), а PlatformIO работает через PowerShell.
rem
rem Своё имя версии:
rem     build-darktec-uf2.cmd -Version v1.17.1b7

setlocal
cd /d "%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-darktec-uf2.ps1" %*
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" (
    echo [OK] Готово. Файл лежит в папке out\uf2
) else (
    echo [ОШИБКА] Сборка не удалась, код %RC%
)
echo.
pause
exit /b %RC%

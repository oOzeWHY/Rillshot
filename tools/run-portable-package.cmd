@echo off
setlocal
chcp 65001 >nul
set "ROOT=%~dp0"
set "APP=%ROOT%app\Rillshot.WinUI.exe"
set "STARTER=%ROOT%support\start-rillshot.ps1"
set "POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"

if not exist "%APP%" (
  echo Rillshot runtime was not found:
  echo %APP%
  pause
  exit /b 2
)
if not exist "%STARTER%" (
  echo Rillshot startup helper not found:
  echo %STARTER%
  pause
  exit /b 2
)
if not exist "%POWERSHELL%" (
  echo Windows PowerShell was not found:
  echo %POWERSHELL%
  pause
  exit /b 2
)

"%POWERSHELL%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%STARTER%" -ExecutablePath "%APP%" -KeepOpen
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
  echo.
  echo Rillshot failed to stay open. Review the startup evidence above.
  echo If the script is blocked as unsigned, run Get-ExecutionPolicy -List.
  pause
)
exit /b %RESULT%

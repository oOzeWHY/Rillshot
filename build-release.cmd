@echo off
setlocal
chcp 65001 >nul
set "ROOT=%~dp0"
set "POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%POWERSHELL%" (
  echo Windows PowerShell was not found: %POWERSHELL%
  pause
  exit /b 2
)
"%POWERSHELL%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\build-release.ps1" -Mode Portable -Platform x64 -Configuration Release -ReleaseStage Preview -PreviewNumber 1 -Clean -SmokeTest
set "RESULT=%ERRORLEVEL%"
echo.
if not "%RESULT%"=="0" (
  echo Build failed with exit code %RESULT%.
  echo Review the first PowerShell error above; later messages may only describe cleanup.
  echo Check ExecutionPolicy only if PowerShell explicitly says that a script is not digitally signed.
) else (
  echo Preview build completed. Check artifacts\release and test the ZIP from a clean directory.
)
pause
exit /b %RESULT%

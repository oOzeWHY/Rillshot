@echo off
setlocal
chcp 65001 >nul
set "RELEASE_ROOT=%~dp0artifacts\release"
set "LAUNCHER="

for /f "delims=" %%F in ('dir /b /s /a-d /o:-d "%RELEASE_ROOT%\Rillshot-*-win-x64-portable\Rillshot.exe" 2^>nul') do if not defined LAUNCHER set "LAUNCHER=%%F"

if not defined LAUNCHER (
  echo Portable build not found.
  echo Run build-release.cmd first.
  pause
  exit /b 2
)

start "" /wait "%LAUNCHER%"
exit /b %ERRORLEVEL%

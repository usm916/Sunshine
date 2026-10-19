@echo off
powershell.exe -NoProfile -File "%~dp0scripts\custom\start-sunshine.ps1" -RequireAdministrator -UseScheduledTask -ExecutablePath "%~dp0sunshine.exe" %*
exit /b %errorlevel%

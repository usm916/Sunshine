@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\custom\rollback-monitor-switch.ps1" -RepositoryRoot "%~dp0." %*
exit /b %errorlevel%

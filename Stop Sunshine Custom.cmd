@echo off
powershell.exe -NoProfile -File "%~dp0scripts\custom\control-sunshine-task.ps1" -Action Stop %*
exit /b %errorlevel%

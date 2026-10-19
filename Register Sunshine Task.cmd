@echo off
powershell.exe -NoProfile -File "%~dp0scripts\custom\register-sunshine-task.ps1" %*
exit /b %errorlevel%

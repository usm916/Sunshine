@echo off
powershell.exe -NoProfile -File "%~dp0scripts\custom\register-sunshine-service.ps1" %*
exit /b %errorlevel%

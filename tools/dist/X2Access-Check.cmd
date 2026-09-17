@echo off
rem Runs the X2Access install check without needing any PowerShell execution-policy change.
rem Double-click this file after extracting the archive at the War of the Chosen game folder.
powershell.exe -NoProfile -NoLogo -ExecutionPolicy Bypass -File "%~dp0X2Access-Check.ps1" %*
echo.
pause

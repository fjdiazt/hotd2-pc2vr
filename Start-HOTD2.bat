@echo off
setlocal
rem Let Windows PowerShell load its own standard modules.
set "PSModulePath="
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-HOTD2.ps1" %*
set "HOTD2_EXIT_CODE=%ERRORLEVEL%"
if not "%HOTD2_EXIT_CODE%"=="0" pause
exit /b %HOTD2_EXIT_CODE%

@echo off
setlocal
"%~dp0HOTD2VR.exe" %*
set "HOTD2_START_EXIT=%ERRORLEVEL%"
if not "%HOTD2_START_EXIT%"=="0" pause
exit /b %HOTD2_START_EXIT%

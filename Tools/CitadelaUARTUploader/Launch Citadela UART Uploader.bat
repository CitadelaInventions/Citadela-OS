@echo off
setlocal
cd /d "%~dp0"
py -3 "%~dp0citadela_uart_uploader.py" %*
if errorlevel 1 pause

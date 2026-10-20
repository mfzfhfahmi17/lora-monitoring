@echo off
cd /d "%~dp0"
if not exist .env (
  copy .env.example .env >nul
  echo File .env dibuat. Silakan isi DEVICE_API_KEY lalu jalankan kembali.
  pause
  exit /b 1
)
npm.cmd start
pause

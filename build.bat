@echo off
setlocal

if not exist "%~dp0PasteLineCounter.c" (
  echo PasteLineCounter.c tidak ditemukan.
  pause
  exit /b 1
)

where gcc >nul 2>nul
if errorlevel 1 (
  echo.
  echo GCC belum ditemukan.
  echo.
  echo Install MSYS2 dari:
  echo https://www.msys2.org/
  echo.
  echo Setelah itu buka "MSYS2 UCRT64" dan jalankan:
  echo pacman -S mingw-w64-ucrt-x86_64-gcc
  echo.
  echo Kemudian jalankan build.bat ini dari:
  echo C:\msys64\ucrt64\bin\bash.exe
  echo atau salin folder ini ke lingkungan UCRT64.
  echo.
  pause
  exit /b 2
)

gcc -O2 -s -mwindows -municode -static-libgcc "%~dp0PasteLineCounter.c" ^
  -o "%~dp0PasteLineCounter.exe" -luser32 -lshell32

if errorlevel 1 (
  echo.
  echo Build gagal.
  pause
  exit /b 3
)

echo.
echo Berhasil: %~dp0PasteLineCounter.exe
echo.
pause

@echo off
rem One-step Windows build.  Output: dist\mcesp.exe + dist\esp-agent.jar
rem Needs: a JDK (javac + jar on PATH) and a 64-bit MinGW-w64 g++ with POSIX threads.
rem g++ is looked up on PATH, then in CLion's bundled MinGW, then in MSYS2.
setlocal
cd /d "%~dp0"
set "ROOT=%cd%"

call agent\build.bat || exit /b 1
cd /d "%ROOT%"

where g++ >nul 2>nul
if not errorlevel 1 goto have_gxx
set "GXXDIR="
for /d %%D in ("%ProgramFiles%\JetBrains\CLion*") do if exist "%%~D\bin\mingw\bin\g++.exe" set "GXXDIR=%%~D\bin\mingw\bin"
for /d %%D in ("%LOCALAPPDATA%\Programs\CLion*") do if exist "%%~D\bin\mingw\bin\g++.exe" set "GXXDIR=%%~D\bin\mingw\bin"
for /d %%D in ("%LOCALAPPDATA%\JetBrains\Toolbox\apps\CLion\ch-0\*") do if exist "%%~D\bin\mingw\bin\g++.exe" set "GXXDIR=%%~D\bin\mingw\bin"
if not defined GXXDIR if exist "C:\msys64\ucrt64\bin\g++.exe" set "GXXDIR=C:\msys64\ucrt64\bin"
if not defined GXXDIR if exist "C:\msys64\mingw64\bin\g++.exe" set "GXXDIR=C:\msys64\mingw64\bin"
if not defined GXXDIR (
  echo.
  echo ERROR: g++ not found. Build from CLion ^(open this folder, it uses CMakeLists.txt^),
  echo or add a MinGW-w64 "bin" folder to PATH, e.g. C:\Program Files\JetBrains\CLion ^<version^>\bin\mingw\bin
  exit /b 1
)
set "PATH=%GXXDIR%;%PATH%"
echo Using g++ from %GXXDIR%

:have_gxx
if not exist dist mkdir dist
rem NOTE: no "-static" here: it fails on newer MinGW toolchains (CLion 2026 / GCC 15). libwinpthread-1.dll is copied instead.
g++ -std=c++20 -O2 -Wall -Wextra src\*.cpp -o dist\mcesp.exe -ld2d1 -ldwrite -ldwmapi -lwinmm -lgdi32 -luser32 -lws2_32 -lwinhttp -lwindowscodecs -lole32 -luuid -lbcrypt -static-libgcc -static-libstdc++ || exit /b 1
for /f "delims=" %%G in ('where g++') do set "GXXEXE=%%G"
for %%G in ("%GXXEXE%") do if exist "%%~dpGlibwinpthread-1.dll" copy /y "%%~dpGlibwinpthread-1.dll" dist\ >nul
copy /y agent\esp-agent.jar dist\ >nul
echo.
echo Done: %ROOT%\dist\mcesp.exe
echo Try it:  cd dist ^&^& mcesp.exe --demo

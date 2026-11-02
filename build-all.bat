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
set "LIBS=-ld2d1 -ldwrite -ldwmapi -lwinmm -lgdi32 -luser32 -lws2_32 -lwinhttp -lwindowscodecs -lole32 -luuid -lbcrypt"
rem The agent jar goes INTO the exe as a resource (ESPAGENT, RCDATA); the exe extracts it at start-up. If windres is missing or fails, the exe is built
rem without it and uses esp-agent.jar next to it (copied below).
set "AGENTOBJ="
set "JARFWD=%ROOT:\=/%/agent/esp-agent.jar"
> dist\agent_resource.rc echo ESPAGENT RCDATA "%JARFWD%"
windres -i dist\agent_resource.rc -o dist\agent_resource.o
if errorlevel 1 (echo NOTE: windres failed - building without the embedded agent.) else set "AGENTOBJ=dist\agent_resource.o"
rem NOTE: no "-static" here: it fails on newer MinGW toolchains (CLion 2026 / GCC 15). First try linking ONLY libwinpthread statically (the exe then needs
rem no DLL); --whole-archive is needed or libstdc++'s weak pthread references stay null. If this toolchain cannot do that, fall back to the DLL.
g++ -std=c++20 -O2 -Wall -Wextra src\*.cpp %AGENTOBJ% -o dist\mcesp.exe %LIBS% -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive,-lwinpthread,--no-whole-archive,-Bdynamic
if not errorlevel 1 (
  echo libwinpthread linked statically: mcesp.exe needs no DLL.
  del /q dist\libwinpthread-1.dll 2>nul
  goto linked
)
echo NOTE: static libwinpthread did not link with this toolchain - using the DLL instead.
g++ -std=c++20 -O2 -Wall -Wextra src\*.cpp %AGENTOBJ% -o dist\mcesp.exe %LIBS% -static-libgcc -static-libstdc++ || exit /b 1
for /f "delims=" %%G in ('where g++') do set "GXXEXE=%%G"
for %%G in ("%GXXEXE%") do if exist "%%~dpGlibwinpthread-1.dll" copy /y "%%~dpGlibwinpthread-1.dll" dist\ >nul
:linked
copy /y agent\esp-agent.jar dist\ >nul
echo.
echo Done: %ROOT%\dist\mcesp.exe  (the agent jar is inside it; dist\esp-agent.jar is only a copy)
echo Try it:  cd dist ^&^& mcesp.exe --demo

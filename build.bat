@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
if exist build\Horizons.exe del build\Horizons.exe

set "RES_OBJ="

rem ---- 1. Icon resource ----
where rc >nul 2>nul
if not errorlevel 1 (
  echo Compiling icon with rc...
  rc /nologo /fo build\resources.res resources.rc
  if errorlevel 1 goto fail
  set "RES_OBJ=build\resources.res"
  goto compile
)
where windres >nul 2>nul
if not errorlevel 1 (
  echo Compiling icon with windres...
  windres -i resources.rc -O coff -o build\resources.o
  if errorlevel 1 goto fail
  set "RES_OBJ=build\resources.o"
  goto compile
)
echo [WARN] No resource compiler found. Building without the icon.

:compile
rem ---- 2. Game ----
where cl >nul 2>nul
if not errorlevel 1 goto use_msvc
where g++ >nul 2>nul
if not errorlevel 1 goto use_gcc

echo [ERROR] No C++ compiler found.
echo Install MinGW-w64 ^(g++^) or Visual Studio Build Tools with the C++ workload.
goto fail

:use_msvc
echo Building with MSVC...
cl /nologo /std:c++17 /O2 /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS src\main.cpp /Fobuild\ /Fe:build\Horizons.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib winmm.lib dwmapi.lib %RES_OBJ%
goto check

:use_gcc
echo Building with g++...
g++ -std=c++17 -O2 -Wall src\main.cpp %RES_OBJ% -o build\Horizons.exe -mwindows -luser32 -lgdi32 -lwinmm -ldwmapi
goto check

:check
if errorlevel 1 (
  echo [ERROR] Compiler reported errors ^(see above^).
  goto fail
)
if not exist build\Horizons.exe (
  echo [ERROR] Build finished but build\Horizons.exe was not created. See linker errors above.
  goto fail
)
echo.
echo Build succeeded: %CD%\build\Horizons.exe
if /i "%~1"=="run" start "" build\Horizons.exe
endlocal
exit /b 0

:fail
echo.
echo Build FAILED.
endlocal
exit /b 1

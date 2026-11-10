@echo off
rem ===================================================================
rem  build-w11.bat -- build with MinGW-w64 (Win10/11 only, for dev)
rem
rem  MinGW builds CRASH on Win98 (CRT startup uses Win9x-missing APIs).
rem  For daily use build with build-tcc.bat instead.
rem
rem  Lookup order for the compiler:
rem    1. environment variable  SHUIHU_GCC  (path to gcc.exe)
rem    2. ..\..\tools\mingw\mingw32\bin\gcc.exe  (default workspace layout)
rem    3. gcc on PATH
rem
rem  Output: bin\shuihu-edit-GUI-mingw.exe
rem          bin\shuihu-edit-CLI-mingw.exe
rem
rem  NOTE: ASCII-only on purpose (see build-tcc.bat for the reason).
rem ===================================================================
setlocal
cd /d "%~dp0.."

set GCC=%SHUIHU_GCC%
if not "%GCC%"=="" goto have_gcc
set GCC=..\..\tools\mingw\mingw32\bin\gcc.exe
if exist "%GCC%" goto have_gcc
where gcc >nul 2>nul
if errorlevel 1 goto no_gcc
set GCC=gcc

:have_gcc
if not exist "%GCC%" goto no_gcc

if not exist src\names_data.c goto need_data
if not exist src\items_data.c goto need_data
if not exist src\human_data.c goto need_data
goto data_ok

:no_gcc
echo.
echo [ERROR] MinGW-w64 gcc not found.
echo   Set SHUIHU_GCC to your gcc.exe, or put gcc on PATH.
echo   (WinLibs / MSYS2 i686 builds with the msvcrt runtime are fine.)
echo.
exit /b 1

:need_data
echo.
echo [ERROR] generated sources are missing:
echo           src\names_data.c  src\items_data.c  src\human_data.c
echo   Generate with:
echo       python tools\gen_data.py "C:\path\to\ShuiHu.org" src
echo   or, to compile without the game (names show as "?"):
echo       python tools\gen_data.py --stub src
echo.
exit /b 1

:data_ok

if not exist bin mkdir bin

set SRC=src\cli.c src\save.c src\fileio.c src\goods.c src\human.c src\tree.c src\textconv.c src\names.c src\names_data.c src\items_data.c src\human_data.c
set SRG=src\ui.c  src\save.c src\fileio.c src\goods.c src\human.c src\tree.c src\textconv.c src\names.c src\names_data.c src\items_data.c src\human_data.c

echo [1/2] console  shuihu-edit-CLI-mingw.exe ...
"%GCC%" -std=c99 -O2 -Wall -Wextra -static -o bin\shuihu-edit-CLI-mingw.exe %SRC%
if errorlevel 1 exit /b 1

echo [2/2] GUI      shuihu-edit-GUI-mingw.exe ...
"%GCC%" -std=c99 -O2 -Wall -Wextra -mwindows -o bin\shuihu-edit-GUI-mingw.exe %SRG% -lcomdlg32
if errorlevel 1 exit /b 1

echo.
echo Done: bin\shuihu-edit-CLI-mingw.exe  bin\shuihu-edit-GUI-mingw.exe
echo (Win10/11 only. Use build-tcc.bat for Win98-compatible builds.)
endlocal

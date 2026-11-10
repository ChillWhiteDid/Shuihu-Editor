@echo off
rem ===================================================================
rem  build-tcc.bat -- build with TinyCC (works on Win98 and Win11)
rem
rem  Requires: TinyCC 0.9.27 (+ winapi-full headers)
rem  Lookup order for the toolchain:
rem    1. environment variable  SHUIHU_TCC   (path to tcc.exe)
rem       and                    SHUIHU_WINAPI (path to winapi include dir)
rem    2. ..\..\tools\tcc\...   (the default layout of this workspace)
rem    3. tcc.exe on PATH
rem
rem  Why TCC: MinGW's CRT startup calls GetModuleHandleW / VirtualProtect,
rem  which do not exist on Win9x -> "illegal operation" on Win98.
rem
rem  Output: bin\shuihu-edit-GUI.exe  (GUI)
rem          bin\shuihu-edit-CLI.exe  (console)
rem
rem  NOTE: ASCII-only on purpose. Chinese in a .bat breaks cmd parsing
rem  when the console code page is not 936 (some GBK trail bytes are
rem  '|' or '^'), and chcp cannot fix that because parsing happens
rem  while reading lines.
rem ===================================================================
setlocal
cd /d "%~dp0.."

set TCC=%SHUIHU_TCC%
set WINAPI=%SHUIHU_WINAPI%
if not "%TCC%"=="" goto have_tcc
set TCC=..\..\tools\tcc\tcc\tcc.exe
set WINAPI=..\..\tools\tcc\winapi-full-for-0.9.27\include\winapi
if exist "%TCC%" goto have_tcc
where tcc >nul 2>nul
if errorlevel 1 goto no_tcc
set TCC=tcc
set WINAPI=%SHUIHU_WINAPI%

:have_tcc
if not exist "%TCC%" goto no_tcc

if not exist src\names_data.c goto need_data
if not exist src\items_data.c goto need_data
if not exist src\human_data.c goto need_data
goto data_ok

:no_tcc
echo.
echo [ERROR] TinyCC not found.
echo   Set SHUIHU_TCC to your tcc.exe, or install it so that "tcc" is on PATH.
echo     tcc      : https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win32-bin.zip
echo     headers  : https://download.savannah.gnu.org/releases/tinycc/winapi-full-for-0.9.27.zip
echo   With the headers unpacked somewhere else, also set SHUIHU_WINAPI to
echo   the "include\winapi" folder.
echo.
exit /b 1

:need_data
echo.
echo [ERROR] generated sources are missing:
echo           src\names_data.c  src\items_data.c  src\human_data.c
echo.
echo   Two ways to create them:
echo.
echo   1) full data, from YOUR OWN copy of the game (needs ShuiHu.org +
echo      Data\GOODS.DAB + Data\HUMAN.HUM):
echo          python tools\gen_data.py "C:\path\to\ShuiHu.org" src
echo.
echo   2) placeholder tables, so the code compiles without the game
echo      (all names show as "?"):
echo          python tools\gen_data.py --stub src
echo.
exit /b 1

:data_ok

if not exist bin mkdir bin

echo [1/2] console  shuihu-edit-CLI.exe ...
"%TCC%" -I "%WINAPI%" -o bin\shuihu-edit-CLI.exe ^
    src\cli.c src\save.c src\fileio.c src\goods.c src\human.c src\tree.c src\textconv.c ^
    src\names.c src\names_data.c src\items_data.c src\human_data.c
if errorlevel 1 exit /b 1

echo [2/2] GUI      shuihu-edit-GUI.exe ...
"%TCC%" -I "%WINAPI%" -o bin\shuihu-edit-GUI.exe ^
    src\ui.c src\save.c src\fileio.c src\goods.c src\human.c src\tree.c src\textconv.c ^
    src\names.c src\names_data.c src\items_data.c src\human_data.c src\comdlg32.def
if errorlevel 1 exit /b 1

echo.
echo Done: bin\shuihu-edit-GUI.exe  bin\shuihu-edit-CLI.exe
echo Both run on Windows 98 / 10 / 11.
endlocal

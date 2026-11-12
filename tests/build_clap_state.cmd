@echo off
setlocal
set "ROOT=%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
if errorlevel 1 exit /b 1
if not exist "%ROOT%\tests\build" mkdir "%ROOT%\tests\build"
cl /nologo /std:c++17 /EHsc /O2 /W3 ^
  /I "%ROOT%\vendor\clap\include" ^
  "%ROOT%\tests\clap_state_smoke.cpp" ^
  /Fe:"%ROOT%\tests\build\clap_state.exe"
exit /b %errorlevel%

@echo off
REM Build nativefiledialog (nfd.lib) for EmissionControl2-Next.
REM
REM Upstream ships only a premake-generated VS2010 project (PlatformToolset v100),
REM which current Visual Studio does not include, so the two sources are compiled
REM directly with the installed toolset.
REM
REM Usage: build_nfd.cmd ["C:\path\to\VsDevCmd.bat"]
REM Normally invoked by tools\bootstrap.ps1, which locates Visual Studio.
setlocal
set "ROOT=%~dp0.."
set "NFD=%ROOT%\ecSource\external\nativefiledialog"
set "VSDEV=%~1"
if "%VSDEV%"=="" set "VSDEV=C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat"

if not exist "%NFD%\src\nfd_win.cpp" (
  echo ERROR: nativefiledialog sources missing. Run tools\bootstrap.ps1 first.
  exit /b 1
)
if not exist "%VSDEV%" (
  echo ERROR: VsDevCmd.bat not found at "%VSDEV%".
  echo Pass its path as the first argument.
  exit /b 1
)

call "%VSDEV%" -arch=x64 -no_logo
if errorlevel 1 exit /b 1

if not exist "%NFD%\build\obj" mkdir "%NFD%\build\obj"
if not exist "%NFD%\build\lib\Release\x64" mkdir "%NFD%\build\lib\Release\x64"

pushd "%NFD%\build\obj"
cl /c /nologo /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE ^
   /I "%NFD%\src\include" /I "%NFD%\src" ^
   "%NFD%\src\nfd_common.c" "%NFD%\src\nfd_win.cpp"
if errorlevel 1 (popd & exit /b 1)

lib /nologo /OUT:"%NFD%\build\lib\Release\x64\nfd.lib" nfd_common.obj nfd_win.obj
if errorlevel 1 (popd & exit /b 1)

popd
echo nfd.lib built: %NFD%\build\lib\Release\x64\nfd.lib
exit /b 0
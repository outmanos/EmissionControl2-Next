@echo off
REM Build an EC2 test program against the static libraries already produced by the
REM Visual Studio 18 / MSVC v145 build in ecSource\build\release.
REM Mirrors the compile and link flags of the EmissionControl2 target.
REM
REM Usage: build_smoke.cmd [source.cpp] [output.exe]
REM        defaults: smoke_ec2.cpp -> smoke.exe
REM        e.g.     build_smoke.cmd bench_voices.cpp bench.exe
REM None of the paths used here contain spaces, so no quoting is needed.
setlocal
set "ROOT=%~dp0.."
set "EC2=%ROOT%\ecSource"
set "REL=%EC2%\build\release"
set "DEP=%ROOT%\build-deps\libsndfile"
set "NFD=%EC2%\external\nativefiledialog\build\lib\Release\x64"

set "SRC=%~1"
set "OUT=%~2"
if "%SRC%"=="" set "SRC=smoke_ec2.cpp"
if "%OUT%"=="" set "OUT=smoke.exe"

call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
if errorlevel 1 exit /b 1
if not exist "%ROOT%\tests\build" mkdir "%ROOT%\tests\build"
pushd "%ROOT%\tests\build"

cl /nologo /std:c++14 /EHsc /O2 /MD /W3 /FIchrono /wd4244 /DOSC_HOST_LITTLE_ENDIAN ^
  /I %EC2%\include /I %EC2%\external /I %EC2%\external\allolib\include ^
  /I %EC2%\external\allolib\external\Gamma /I %EC2%\external\allolib\external\rtaudio ^
  /I %EC2%\external\allolib\external\rtmidi /I %EC2%\external\allolib\external\imgui ^
  /I %EC2%\external\allolib\external\glad\include /I %EC2%\external\allolib\external\json\include ^
  /I %EC2%\external\allolib\external\cpptoml\include /I %DEP%\include ^
  /I %EC2%\external\allolib\external\glfw\include /I %EC2%\external\allolib\external\dr_libs\dr_libs ^
  /I %EC2%\external\allolib\external\json\single_include /I %EC2%\external\allolib\external\oscpack ^
  /I %EC2%\external\allolib\external\stb\stb /I %EC2%\external\allolib\external\serial\include ^
  /I %EC2%\external\libsamplerate\src /I %REL%\external\libsamplerate ^
  "%ROOT%\tests\%SRC%" ^
  /Fe:%OUT% ^
  /link ^
  %REL%\Release\ecLib.lib ^
  %REL%\external\al_ext\soundfile\Release\al_soundfile.lib ^
  %REL%\external\allolib\Release\al.lib ^
  %REL%\external\allolib\external\Gamma\lib\Gamma.lib ^
  %REL%\external\allolib\external\glfw\src\Release\glfw3.lib ^
  %REL%\external\allolib\external\glad\Release\glad.lib ^
  %REL%\external\allolib\external\rtaudio\Release\rtaudio.lib ^
  %REL%\external\allolib\external\rtmidi\Release\rtmidi.lib ^
  %REL%\external\allolib\external\Release\imgui.lib ^
  %REL%\external\allolib\external\Release\oscpack.lib ^
  %REL%\external\allolib\external\Release\serial.lib ^
  %REL%\external\libsamplerate\Release\samplerate.lib ^
  %DEP%\lib\sndfile.lib ^
  %NFD%\nfd.lib ^
  opengl32.lib ksuser.lib mfplat.lib mfuuid.lib wmcodecdspuuid.lib ole32.lib Ws2_32.lib ^
  winmm.lib setupapi.lib comctl32.lib kernel32.lib user32.lib gdi32.lib winspool.lib ^
  shell32.lib oleaut32.lib uuid.lib comdlg32.lib advapi32.lib
if errorlevel 1 (popd & exit /b 1)
popd
exit /b 0
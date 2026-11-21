@echo off
rem Stage00's bounded offline driver uses a fresh build/install prefix for each qualified slice.
rem Argument1 names the slice; argument2 is configure/build/test/install; argument3 is Release/Debug.
if not "%~1"=="planes" exit /b 2
call "C:\Users\12650029\serene-academic\serene-math\load_compilers.bat"
if errorlevel 1 exit /b %errorlevel%
set "S00_SOURCE=%~dp0.."
set "S00_BUILD=%~dp0../build/visualization_S00/%~1/combined-msvc"
set "S00_INSTALL=%~dp0../build/visualization_S00/%~1/combined-install"
rem Refuse every action once this slice is sealed; use the next fresh slice instead.
if exist "%~dp0../build/visualization_S00/%~1/qualification-v1/manifest.json" exit /b 3
if "%~2"=="configure" goto configure
if not "%~3"=="Release" if not "%~3"=="Debug" exit /b 2
if "%~2"=="build" goto build
if "%~2"=="test" goto test
if "%~2"=="install" goto install
echo Usage: build_visualization_overlays.cmd planes configure ^| planes build/test/install Release/Debug
exit /b 2
:configure
rem Configure matching archive/model/codec/pose components without network dependency fallback.
cmake -S "%S00_SOURCE%" -B "%S00_BUILD%" -G "Visual Studio 18 2026" -A x64 -T host=x64 -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DSERENE_OUTPUTS_BUILD_CODEC=ON -DSERENE_OUTPUTS_BUILD_ARCHIVE=ON -DSERENE_OUTPUTS_BUILD_POSE_RECORDER=ON -DSERENE_OUTPUTS_DEPENDENCY_MODE=PINNED -DSERENE_OUTPUTS_ZFP_ARCHIVE="%S00_SOURCE%/build/dependencies-extensions/zfp-1.0.1.tar.gz" -DPython3_EXECUTABLE="C:/Users/12650029/AppData/Local/anaconda3/envs/eic12/python.exe"
exit /b %errorlevel%
:build
rem Build changed recorder tests, old pose regressions and the small unchanged archive check only.
cmake --build "%S00_BUILD%" --config %3 --parallel 2 --target serene_outputs_model serene_outputs_codec serene_outputs_archive serene_outputs_pose_recorder test_pose_recorder test_winch_recorder test_fluid_recorder test_applied_recorder test_moment_recorder test_plane_recorder test_archive
exit /b %errorlevel%
:test
rem Each producer wrapper creates a fresh attempt directory; no old qualification tree is regenerated.
ctest --test-dir "%S00_BUILD%" -C %3 -R "^(serene_outputs_pose_.*|serene_outputs_archive)$" --output-on-failure
exit /b %errorlevel%
:install
rem Both runtime configurations share this new slice prefix; earlier slice and J07 installs survive.
cmake --install "%S00_BUILD%" --config %3 --prefix "%S00_INSTALL%"
exit /b %errorlevel%

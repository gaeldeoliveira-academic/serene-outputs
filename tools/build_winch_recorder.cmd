@echo off
rem J07's bounded, offline combined package driver. No old qualification tree is reused.
rem Argument1 is configure/build/test/install; argument2 selects Release or Debug except configure.
rem Source/runtime/feature options match the existing maintained MSVC build contract.
call "C:\Users\12650029\serene-academic\serene-math\load_compilers.bat"
if errorlevel 1 exit /b %errorlevel%
set "J07_SOURCE=%~dp0.."
set "J07_BUILD=%~dp0../build/batch-j/J07/combined-msvc"
set "J07_INSTALL=%~dp0../build/batch-j/J07/combined-install"
if "%~1"=="configure" goto configure
if not "%~2"=="Release" if not "%~2"=="Debug" exit /b 2
if "%~1"=="build" goto build
if "%~1"=="test" goto test
if "%~1"=="install" goto install
echo Usage: build_winch_recorder.cmd configure ^| build/test/install Release/Debug
exit /b 2
:configure
rem Configure all four components in one physical include/library view, without network fallback.
cmake -S "%J07_SOURCE%" -B "%J07_BUILD%" -G "Visual Studio 18 2026" -A x64 -T host=x64 -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DSERENE_OUTPUTS_BUILD_CODEC=ON -DSERENE_OUTPUTS_BUILD_ARCHIVE=ON -DSERENE_OUTPUTS_BUILD_POSE_RECORDER=ON -DSERENE_OUTPUTS_DEPENDENCY_MODE=PINNED -DSERENE_OUTPUTS_ZFP_ARCHIVE="%J07_SOURCE%/build/dependencies-extensions/zfp-1.0.1.tar.gz" -DPython3_EXECUTABLE="C:/Users/12650029/AppData/Local/anaconda3/envs/eic12/python.exe"
exit /b %errorlevel%
:build
rem Build the required installed libraries plus focused new/legacy pose and small archive checks.
cmake --build "%J07_BUILD%" --config %2 --parallel 2 --target serene_outputs_model serene_outputs_codec serene_outputs_archive serene_outputs_pose_recorder test_pose_recorder test_winch_recorder test_archive
exit /b %errorlevel%
:test
rem Tests author only fresh attempt outputs; codec exponent sweeps and solver studies are not selected.
ctest --test-dir "%J07_BUILD%" -C %2 -R "^(serene_outputs_pose_.*|serene_outputs_archive)$" --output-on-failure
exit /b %errorlevel%
:install
rem Install each runtime configuration into the same fresh J07 prefix; preserve historical prefixes.
cmake --install "%J07_BUILD%" --config %2 --prefix "%J07_INSTALL%"
exit /b %errorlevel%

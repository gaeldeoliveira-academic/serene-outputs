@echo off
call "C:\Users\12650029\serene-academic\serene-math\load_compilers.bat"
if errorlevel 1 exit /b %errorlevel%
cmake -S "C:/Users/12650029/serene-academic/serene-outputs" -B "C:/Users/12650029/serene-academic/serene-outputs/build/extensions-msvc" -G "Visual Studio 18 2026" -A x64 -T host=x64 -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DSERENE_OUTPUTS_ZFP_ARCHIVE="C:/Users/12650029/serene-academic/serene-outputs/build/dependencies-extensions/zfp-1.0.1.tar.gz"
if errorlevel 1 exit /b %errorlevel%
cmake --build "C:/Users/12650029/serene-academic/serene-outputs/build/extensions-msvc" --config %1 --parallel 4
exit /b %errorlevel%

@echo off
call "C:\Users\12650029\serene-academic\serene-math\load_compilers.bat"
if errorlevel 1 exit /b %errorlevel%
set "CC=cl"
set "CXX=cl"
"C:\Users\12650029\serene\build-tools\cmake-4.2.2-windows-x86_64\cmake-4.2.2-windows-x86_64\bin\cmake.exe" -S . -B build\extensions-codec-msvc -G "Visual Studio 18 2026" -A x64 -DSERENE_OUTPUTS_BUILD_ARCHIVE=OFF -DSERENE_OUTPUTS_BUILD_CODEC=ON -DBUILD_TESTING=ON -DSERENE_OUTPUTS_ZFP_ARCHIVE=C:/Users/12650029/CFD/snapshot_compressor/src/cpp/build/_deps/zfp-subbuild/zfp-populate-prefix/src/f40868a6a1c190c802e7d8b5987064f044bf7812.tar.gz -DCMAKE_INSTALL_PREFIX=C:/Users/12650029/serene-academic/serene-outputs/build/unused-codec-only-prefix

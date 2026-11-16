@echo off
rem Maintained local CMake driver; argument 1 selects the multi-config build (Release or Debug).
rem Load the established MSVC environment, configure the named isolated build tree, then build its focused targets.
rem Existing absolute dependency/compiler pins are intentional local qualification inputs, not portable defaults.
rem Establish the matching compiler and runtime environment before invoking CMake.
call "C:\Users\12650029\serene-academic\serene-math\load_compilers.bat"
if errorlevel 1 exit /b %errorlevel%
rem Configure with the existing target/dependency options unchanged.
cmake -S "C:/Users/12650029/serene-academic/serene-outputs" -B "C:/Users/12650029/serene-academic/serene-outputs/build/document-preflight-msvc" -G "Visual Studio 18 2026" -A x64 -T host=x64 -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DSERENE_OUTPUTS_ZFP_ARCHIVE="C:/Users/12650029/serene-academic/serene-outputs/build/dependencies-extensions/zfp-1.0.1.tar.gz"
if errorlevel 1 exit /b %errorlevel%
rem Build only; execution/qualification is performed by the owning workflow.
cmake --build "C:/Users/12650029/serene-academic/serene-outputs/build/document-preflight-msvc" --config %1 --parallel 4 --target test_document_preflight test_bundle
exit /b %errorlevel%

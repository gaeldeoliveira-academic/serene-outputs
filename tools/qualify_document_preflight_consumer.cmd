@echo off
rem Installed-consumer qualification driver for an already prepared source/build/prefix workspace.
rem The named consumer variable owns its local root path; no product source tree is added to the consumer.
rem Configure the isolated package lookup and require both Release and Debug build/CTest success.
rem Establish the matching compiler and runtime environment before invoking CMake.
call "C:\Users\12650029\serene-academic\serene-math\load_compilers.bat"
if errorlevel 1 exit /b %errorlevel%
set "DOCUMENT_CONSUMER=C:/Users/12650029/serene-academic/serene-theory/validation/outputs_document_preflight_consumer"
rem Configure with the existing target/dependency options unchanged.
cmake -S "%DOCUMENT_CONSUMER%/source" -B "%DOCUMENT_CONSUMER%/build" -G "Visual Studio 18 2026" -A x64 -T host=x64 -DCMAKE_CXX_COMPILER=cl -DCMAKE_PREFIX_PATH="%DOCUMENT_CONSUMER%/prefix"
if errorlevel 1 exit /b %errorlevel%
rem B names the configuration; each failure exits before the next phase.
for %%B in (Release Debug) do (
  cmake --build "%DOCUMENT_CONSUMER%/build" --config %%B --parallel 4
  if errorlevel 1 exit /b 1
  ctest --test-dir "%DOCUMENT_CONSUMER%/build" -C %%B --output-on-failure
  if errorlevel 1 exit /b 1
)
exit /b 0

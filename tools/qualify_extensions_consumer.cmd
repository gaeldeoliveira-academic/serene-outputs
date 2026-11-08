@echo off
call "C:\Users\12650029\serene-academic\serene-math\load_compilers.bat"
if errorlevel 1 exit /b %errorlevel%
set "EXTENSIONS_CONSUMER=C:/Users/12650029/serene-academic/serene-theory/validation/outputs_extensions_consumer"
for %%C in (model codec archive) do (
  cmake -S "%EXTENSIONS_CONSUMER%/source" -B "%EXTENSIONS_CONSUMER%/build-%%C" -G "Visual Studio 18 2026" -A x64 -T host=x64 -DCMAKE_CXX_COMPILER=cl -DCMAKE_PREFIX_PATH="%EXTENSIONS_CONSUMER%/prefix" -DCONSUMER_COMPONENT=%%C
  if errorlevel 1 exit /b 1
  for %%B in (Release Debug) do (
    cmake --build "%EXTENSIONS_CONSUMER%/build-%%C" --config %%B --parallel 4
    if errorlevel 1 exit /b 1
    ctest --test-dir "%EXTENSIONS_CONSUMER%/build-%%C" -C %%B --output-on-failure
    if errorlevel 1 exit /b 1
  )
)
exit /b 0

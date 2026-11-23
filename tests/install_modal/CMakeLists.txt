# Standalone relocated SDK consumer: no source-tree, backend, JSON or codec headers.
cmake_minimum_required(VERSION 3.28)
project(InstalledModal LANGUAGES CXX)
find_package(SereneOutputs 0.3.0 EXACT CONFIG REQUIRED COMPONENTS archive)
add_executable(installed_modal main.cpp)
target_link_libraries(installed_modal PRIVATE serene::outputs_archive)
set_target_properties(installed_modal PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

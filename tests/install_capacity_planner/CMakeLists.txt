# Standalone installed-consumer probe; configure against the intended preserved prefix.
# Version pins and component-isolation checks qualify that historical package,
# rather than asserting that all installed versions are interchangeable.
cmake_minimum_required(VERSION 3.28)
project(InstalledCapacityPlanner LANGUAGES CXX)
# Import only the named component and its declared dependencies.
find_package(SereneOutputs 0.2.1 EXACT CONFIG REQUIRED COMPONENTS model)
if(TARGET serene::outputs_codec OR TARGET serene::outputs_archive OR TARGET serene::outputs_zfp_dependency)
  message(FATAL_ERROR "The count planner must import only the model component")
endif()
# Compile the public-API consumer with the matching Windows CRT family.
add_executable(installed_capacity_planner main.cpp)
target_link_libraries(installed_capacity_planner PRIVATE serene::outputs_model)
set_target_properties(installed_capacity_planner PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
# Register this focused consumer invocation after successful package import.
enable_testing()
add_test(NAME installed_capacity_planner COMMAND installed_capacity_planner)

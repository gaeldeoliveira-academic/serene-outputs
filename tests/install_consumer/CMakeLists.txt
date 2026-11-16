# Standalone installed-consumer probe; configure against the intended preserved prefix.
# Version pins and component-isolation checks qualify that historical package,
# rather than asserting that all installed versions are interchangeable.
cmake_minimum_required(VERSION 3.28)
project(SereneOutputsInstalledConsumer LANGUAGES CXX)
# CONSUMER_COMPONENT selects both the imported target and matching probe source.
set(CONSUMER_COMPONENT model CACHE STRING "Installed component to exercise")
# Import only the named component and its declared dependencies.
find_package(SereneOutputs CONFIG REQUIRED COMPONENTS ${CONSUMER_COMPONENT})
if(CONSUMER_COMPONENT STREQUAL "model")
  if(TARGET serene::outputs_codec OR TARGET serene::outputs_archive)
    message(FATAL_ERROR "Model-only package import unexpectedly loaded native dependencies")
  endif()
elseif(CONSUMER_COMPONENT STREQUAL "codec")
  if(TARGET serene::outputs_archive)
    message(FATAL_ERROR "Codec-only package import unexpectedly loaded the archive backend")
  endif()
endif()
# Compile the public-API consumer with the matching Windows CRT family.
add_executable(installed_consumer ${CONSUMER_COMPONENT}.cpp)
target_link_libraries(installed_consumer PRIVATE serene::outputs_${CONSUMER_COMPONENT})
set_target_properties(installed_consumer PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
# Register this focused consumer invocation after successful package import.
enable_testing()
add_test(NAME installed_${CONSUMER_COMPONENT} COMMAND installed_consumer)

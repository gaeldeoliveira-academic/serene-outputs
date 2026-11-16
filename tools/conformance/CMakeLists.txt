# Independent trusted-fixture inspector: intentionally separate from production libraries.
# Preserve the explicit Windows BCrypt and matching static-runtime public-ZFP setup.
cmake_minimum_required(VERSION 3.25)
project(SereneOutputsN01Conformance LANGUAGES CXX)
if(NOT MSVC)
  message(FATAL_ERROR "This isolated N01 inspector uses Windows BCrypt; other platforms are not qualified here")
endif()
# This historical inspector requires matching /MT public-ZFP inputs; it is not the production /MD package ABI.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(N01_ZFP_INCLUDE_DIR "" CACHE PATH "Pinned research ZFP include directory")
set(N01_ZFP_LIBRARY "" CACHE FILEPATH "Pinned /MT ZFP library for isolated fixture inspection")
if(NOT EXISTS "${N01_ZFP_INCLUDE_DIR}/zfp.h" OR NOT EXISTS "${N01_ZFP_LIBRARY}")
 message(FATAL_ERROR "Supply the pinned matching /MT research ZFP include/library explicitly")
endif()
# Build only this independent byte inspector using the explicitly supplied public dependency.
add_executable(inspect_records inspect_records.cpp)
target_compile_features(inspect_records PRIVATE cxx_std_23)
target_compile_definitions(inspect_records PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(inspect_records PRIVATE "${N01_ZFP_INCLUDE_DIR}")
target_link_libraries(inspect_records PRIVATE "${N01_ZFP_LIBRARY}" bcrypt)
# Register the existing tiny fixture check; no fixture author or production writer runs implicitly.
enable_testing()
add_test(NAME n01_independent_records COMMAND inspect_records "${CMAKE_CURRENT_SOURCE_DIR}/../../fixtures")

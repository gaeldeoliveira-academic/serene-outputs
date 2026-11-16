# Independent trusted-fixture inspector: intentionally separate from production libraries.
# Preserve the explicit Windows BCrypt and matching static-runtime public-ZFP setup.
cmake_minimum_required(VERSION 3.25)
project(SereneOutputsG0CInspector LANGUAGES CXX)
if(NOT MSVC)
 message(FATAL_ERROR "Isolated Windows BCrypt fixture inspector; other platforms not qualified")
endif()
# This historical inspector requires matching /MT public-ZFP inputs; it is not the production /MD package ABI.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(G0C_ZFP_INCLUDE_DIR "" CACHE PATH "Pinned public research ZFP headers")
set(G0C_ZFP_LIBRARY "" CACHE FILEPATH "Matching isolated /MT ZFP library")
if(NOT EXISTS "${G0C_ZFP_INCLUDE_DIR}/zfp.h" OR NOT EXISTS "${G0C_ZFP_LIBRARY}")
 message(FATAL_ERROR "Supply exact pinned /MT ZFP headers/library")
endif()
# Build only this independent byte inspector using the explicitly supplied public dependency.
add_executable(inspect_container inspect_container.cpp)
target_compile_features(inspect_container PRIVATE cxx_std_23)
target_compile_definitions(inspect_container PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(inspect_container PRIVATE "${G0C_ZFP_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/nlohmann_json/include")
target_link_libraries(inspect_container PRIVATE "${G0C_ZFP_LIBRARY}" bcrypt)
# Register the existing tiny fixture check; no fixture author or production writer runs implicitly.
enable_testing()
add_test(NAME g0c_independent_container COMMAND inspect_container "${CMAKE_CURRENT_SOURCE_DIR}/../../../fixtures_custom")

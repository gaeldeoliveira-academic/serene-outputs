# Standalone installed-consumer probe; configure against the intended preserved prefix.
# Version pins and component-isolation checks qualify that historical package,
# rather than asserting that all installed versions are interchangeable.
cmake_minimum_required(VERSION 3.28)
project(InstalledDocumentPreflight LANGUAGES CXX)
# Import only the named component and its declared dependencies.
find_package(SereneOutputs 0.2.2 EXACT CONFIG REQUIRED COMPONENTS archive)
# Compile the public-API consumer with the matching Windows CRT family.
add_executable(installed_document_preflight main.cpp)
target_link_libraries(installed_document_preflight PRIVATE serene::outputs_archive)
set_target_properties(installed_document_preflight PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
# Register this focused consumer invocation after successful package import.
enable_testing()
add_test(NAME installed_document_preflight COMMAND installed_document_preflight)

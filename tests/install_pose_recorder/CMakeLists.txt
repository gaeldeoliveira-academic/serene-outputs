# Standalone installed-consumer probe; configure against the intended preserved prefix.
# Version pins and component-isolation checks qualify that historical package,
# rather than asserting that all installed versions are interchangeable.
cmake_minimum_required(VERSION 3.28)
project(SerenePoseRecorderConsumer LANGUAGES CXX)
# Import only the named component and its declared dependencies.
find_package(SereneOutputs 0.2.3 CONFIG REQUIRED COMPONENTS pose_recorder)
# unwanted enumerates components forbidden in a recorder-only import.
foreach(unwanted outputs_model outputs_codec outputs_archive outputs_zfp_dependency)
  if(TARGET serene::${unwanted})
    message(FATAL_ERROR "Recorder-only import unexpectedly loaded ${unwanted}")
  endif()
endforeach()
# Compile the public-API consumer with the matching Windows CRT family.
add_executable(pose_consumer main.cpp)
target_link_libraries(pose_consumer PRIVATE serene::outputs_pose_recorder)
set_target_properties(pose_consumer PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

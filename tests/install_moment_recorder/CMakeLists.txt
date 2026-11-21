# Standalone public API consumer for one relocated signed-moment installation.
cmake_minimum_required(VERSION 3.28)
project(SereneMomentRecorderConsumer LANGUAGES CXX)
# Select whether this consumer must prove combined linkage or isolated pose imports.
option(SERENE_MOMENT_CONSUMER_ARCHIVE "Also exercise the scientific archive component" ON)

# Select components explicitly so pose-only qualification has no archive dependencies.
if(SERENE_MOMENT_CONSUMER_ARCHIVE)
  find_package(SereneOutputs 0.2.7 EXACT CONFIG REQUIRED COMPONENTS archive pose_recorder)
  # required is the unqualified name of each target the combined import must expose.
  foreach(required outputs_model outputs_codec outputs_archive outputs_zfp_dependency outputs_pose_recorder)
    if(NOT TARGET serene::${required})
      message(FATAL_ERROR "Combined import omitted required target ${required}")
    endif()
  endforeach()
else()
  find_package(SereneOutputs 0.2.7 EXACT CONFIG REQUIRED COMPONENTS pose_recorder)
  if(NOT TARGET serene::outputs_pose_recorder)
    message(FATAL_ERROR "Pose-only import omitted its required recorder target")
  endif()
  # forbidden identifies an archive/codec target that pose-only import must not create.
  foreach(forbidden outputs_model outputs_codec outputs_archive outputs_zfp_dependency)
    if(TARGET serene::${forbidden})
      message(FATAL_ERROR "Pose-only import unexpectedly exposed target ${forbidden}")
    endif()
  endforeach()
endif()

# The consumer includes only installed public headers and has no fixture JSON dependency.
add_executable(moment_consumer main.cpp)
target_link_libraries(moment_consumer PRIVATE serene::outputs_pose_recorder)
target_compile_definitions(moment_consumer PRIVATE SERENE_MOMENT_CONSUMER_ARCHIVE=$<BOOL:${SERENE_MOMENT_CONSUMER_ARCHIVE}>)
if(SERENE_MOMENT_CONSUMER_ARCHIVE)
  target_link_libraries(moment_consumer PRIVATE serene::outputs_archive)
endif()
# Match the installed package's /MD Release and /MDd Debug runtime selection.
set_target_properties(moment_consumer PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

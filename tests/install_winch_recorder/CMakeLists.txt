# Standalone public-API probe for one relocated combined installation.
cmake_minimum_required(VERSION 3.28)
project(SereneWinchRecorderConsumer LANGUAGES CXX)
# Require both components from the same exact package version and dependency graph.
find_package(SereneOutputs 0.2.4 EXACT CONFIG REQUIRED COMPONENTS archive pose_recorder)
foreach(required outputs_model outputs_codec outputs_archive outputs_zfp_dependency outputs_pose_recorder)
  if(NOT TARGET serene::${required})
    message(FATAL_ERROR "Combined import omitted required target ${required}")
  endif()
endforeach()
# The source includes public installed headers only; no fixture JSON parser is linked.
add_executable(winch_consumer main.cpp)
target_link_libraries(winch_consumer PRIVATE serene::outputs_archive serene::outputs_pose_recorder)
set_target_properties(winch_consumer PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

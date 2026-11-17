cmake_minimum_required(VERSION 3.21)
project(abm_projectm_bridge LANGUAGES CXX)
find_package(projectM4 CONFIG REQUIRED)
add_library(abm_projectm_bridge STATIC projectm_visualizer.cpp)
target_compile_features(abm_projectm_bridge PRIVATE cxx_std_17)
target_include_directories(abm_projectm_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(abm_projectm_bridge PUBLIC libprojectM::projectM opengl32)
set_target_properties(abm_projectm_bridge PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

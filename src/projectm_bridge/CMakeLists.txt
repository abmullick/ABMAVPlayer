cmake_minimum_required(VERSION 3.21)
project(abm_projectm_bridge LANGUAGES CXX)
find_package(projectM4 CONFIG REQUIRED)

# This small DLL is loaded by ABM. It only launches/manages the renderer and
# exchanges PCM/control state through shared memory; it never links OpenGL or
# ProjectM and therefore cannot take a GPU/ProjectM fault into the player.
add_library(abm_projectm_bridge SHARED ../projectm_host.cpp)
target_compile_features(abm_projectm_bridge PRIVATE cxx_std_17)
target_compile_definitions(abm_projectm_bridge PRIVATE ABM_PROJECTM_BUILD)
target_include_directories(abm_projectm_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(abm_projectm_bridge PRIVATE user32)

# All ProjectM/OpenGL work runs in this disposable helper process.
add_executable(abm-projectm-renderer ../projectm_renderer.cpp)
target_compile_features(abm-projectm-renderer PRIVATE cxx_std_17)
target_include_directories(abm-projectm-renderer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(abm-projectm-renderer PRIVATE libprojectM::projectM opengl32 user32 gdi32)
set_target_properties(abm_projectm-renderer PROPERTIES
  OUTPUT_NAME "abm-projectm-renderer"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set_target_properties(abm_projectm_bridge PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

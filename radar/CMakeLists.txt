cmake_minimum_required(VERSION 3.20)
project(ac_sim LANGUAGES C CXX)

# Modern Apple and Windows linkers do not need repeated static archives. Keep
# the explicit target graph readable while letting capable CMake versions emit
# a deterministic, de-duplicated link line.
if(POLICY CMP0156)
  cmake_policy(SET CMP0156 NEW)
endif()

# Radar is intentionally self-contained: no target includes or links Vision.
if(MSVC)
  enable_language(ASM_MASM)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(RADAR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(RADAR_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_library(lr_build_options INTERFACE)

function(lr_library target)
  if(NOT ARGN)
    message(FATAL_ERROR "${target} has no sources in the Radar layout")
  endif()
  add_library(${target} ${ARGN})
  target_link_libraries(${target} PUBLIC lr_build_options)
endfunction()

function(lr_exe target source)
  if(NOT EXISTS "${RADAR_SOURCE_DIR}/${source}")
    message(FATAL_ERROR "${target} source is missing: ${source}")
  endif()
  add_executable(${target} "${RADAR_SOURCE_DIR}/${source}")
  target_link_libraries(${target} PRIVATE lr_build_options ${ARGN})
  # Applications are composition roots; reusable targets never receive this path.
  target_include_directories(${target} PRIVATE "${RADAR_SOURCE_DIR}/adapters")
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/cmake/Options.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/DomainTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/SimulationTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/ScenarioTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/RealTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/AppTargets.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/TestTargets.cmake")
enable_testing()
add_subdirectory(tests)

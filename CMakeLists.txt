# Minimum required version of CMake
cmake_minimum_required(VERSION 3.18)

# Set CMake project name
project(RTCMP)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMake")

find_package(Threads REQUIRED)
find_package(BRLCAD REQUIRED)

find_library(M_LIBRARY m)

if(NOT MSVC)
  add_definitions(-g)
endif(NOT MSVC)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${BRLCAD_INCLUDE_DIRS}
  )

set(rtcmp_SRCS
  dry/dry.cpp
  jsoncmp.cpp
  perfcomp.cpp
  shotset.cpp
  rt/rt_diff.cpp
  rt/rt_perf.cpp
  rtcmp.cpp
  tie/tie_diff.cpp
  tie/tie_perf.cpp
  )

add_executable(rtcmp ${rtcmp_SRCS})
set_property(TARGET rtcmp PROPERTY CXX_STANDARD 17)
target_compile_definitions(rtcmp PRIVATE RT_DLL_IMPORTS)
target_link_libraries(rtcmp PRIVATE Threads::Threads ${BRLCAD_LIBRARIES} $<$<BOOL:${M_LIBRARY}>:${M_LIBRARY}>)

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8

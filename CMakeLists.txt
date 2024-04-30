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
  json/rt_json.cpp
  jsoncmp.cpp
  #tie/tie.c
  #dry/dry.c
  #partm.c
  perfcomp.cpp
  #rt/rt.c
  rtcmp.cpp
  )

set(rtcmp_HDRS
  json/json.hpp
  json/rt_json.h
  tie/tie.h
  dry/dry.h
  perfcomp.h
  rt/rt.h
  rtcmp.h
  )

add_executable(rtcmp ${rtcmp_SRCS})
target_link_libraries(rtcmp PRIVATE Threads::Threads ${BRLCAD_LIBRARIES} ${M_LIBRARY})

#	./rtcmp -a -b -r -s /usr/brlcad/share/brlcad/7.?.0/db/ktank.g tank computer g17 engine


# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8

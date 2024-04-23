# Minimum required version of CMake
cmake_minimum_required(VERSION 3.18)

# Set CMake project name
project(RTCMP)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMake")

find_package(Threads REQUIRED)
find_package(BRLCAD REQUIRED)

find_library(M_LIBRARY m)

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}
	${BRLCAD_INCLUDE_DIRS}
	)

set(rtcmp_SRCS
	adrt/adrt.cpp
	adrt/gfile.cpp
	dry/dry.c
	partm.c
	perfcomp.c
	rayforce/rayforce.c
	rt/rt.c
	rtcmp.c
	)

set(rtcmp_HDRS
	adrt/adrt.h
	adrt/adrt_struct.h
	adrt/gfile.h
	dry/dry.h
	perfcomp.h
	rayforce/rayforce.h
	rt/rt.h
	rtcmp.h
	)

add_executable(rtcmp ${rtcmp_SRCS})
target_link_libraries(rtcmp PRIVATE Threads::Threads ${BRLCAD_LIBRARIES} ${M_LIBRARY})

#	./rtcmp -a -b -r -s /usr/brlcad/share/brlcad/7.?.0/db/ktank.g tank computer g17 engine


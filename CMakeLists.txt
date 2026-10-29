# Minimum required version of CMake
cmake_minimum_required(VERSION 3.18)

# Set CMake project name
project(RTCMP)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMake")

find_package(Threads REQUIRED)

if(NOT DEFINED BRLCAD_ROOT AND NOT "$ENV{BRLCAD_ROOT}" STREQUAL "")
  set(BRLCAD_ROOT "$ENV{BRLCAD_ROOT}")
endif()
set(_rtcmp_saved_module_path "${CMAKE_MODULE_PATH}")
set(_rtcmp_saved_prefix_path "${CMAKE_PREFIX_PATH}")
find_package(BRLCAD CONFIG QUIET COMPONENTS rt bn bu HINTS "${BRLCAD_ROOT}" ENV BRLCAD_ROOT)
if(NOT BRLCAD_FOUND)
  set(CMAKE_MODULE_PATH "${_rtcmp_saved_module_path}")
  set(CMAKE_PREFIX_PATH "${_rtcmp_saved_prefix_path}")
  find_package(BRLCAD MODULE REQUIRED)
endif()
unset(_rtcmp_saved_module_path)
unset(_rtcmp_saved_prefix_path)
if(NOT BRLCAD_ROOT AND DEFINED PACKAGE_PREFIX_DIR)
  set(BRLCAD_ROOT "${PACKAGE_PREFIX_DIR}")
endif()

find_library(M_LIBRARY m)

if(NOT MSVC)
  add_definitions(-g)
endif(NOT MSVC)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${BRLCAD_INCLUDE_DIRS}
  )

set(rtcmp_core_SRCS
  comp/jsoncmp.cpp
  comp/grazing.cpp
  comp/shot_comp.cpp
  util/util.cpp
  util/ray_io.cpp
  )

add_library(rtcmp_core STATIC ${rtcmp_core_SRCS})
target_compile_features(rtcmp_core PUBLIC cxx_std_17)
target_compile_definitions(rtcmp_core PUBLIC RT_DLL_IMPORTS BU_DLL_IMPORTS)
target_link_libraries(rtcmp_core PUBLIC Threads::Threads BRLCAD::rt BRLCAD::bn BRLCAD::bu $<$<BOOL:${M_LIBRARY}>:${M_LIBRARY}>)

set(rtcmp_SRCS
  dry/dry.cpp
  perf/perfcomp.cpp
  rt/librt_engine.cpp
  rt/rt.cpp
  rtcmp.cpp
  tie/tie.cpp
  )

add_executable(rtcmp ${rtcmp_SRCS})
target_link_libraries(rtcmp PRIVATE rtcmp_core)

include(CTest)
if(BUILD_TESTING)
  add_executable(rayset_test tests/rayset_test.cpp)
  target_link_libraries(rayset_test PRIVATE rtcmp_core)
  add_test(NAME rayset_test
    COMMAND ${CMAKE_COMMAND}
      "-DTEST_EXECUTABLE=$<TARGET_FILE:rayset_test>"
      "-DRTCMP_RUNTIME_DIRECTORY=${BRLCAD_ROOT}/bin"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_test.cmake"
    )

  add_executable(correctness_test tests/correctness_test.cpp)
  target_link_libraries(correctness_test PRIVATE rtcmp_core)
  add_test(NAME correctness_test
    COMMAND ${CMAKE_COMMAND}
      "-DTEST_EXECUTABLE=$<TARGET_FILE:correctness_test>"
      "-DRTCMP_RUNTIME_DIRECTORY=${BRLCAD_ROOT}/bin"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_test.cmake"
    )

  add_test(NAME rtcmp_runtime_smoke
    COMMAND ${CMAKE_COMMAND}
      "-DTEST_EXECUTABLE=$<TARGET_FILE:rtcmp>"
      "-DTEST_ARGUMENTS=--help"
      "-DRTCMP_RUNTIME_DIRECTORY=${BRLCAD_ROOT}/bin"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_test.cmake"
    )

  add_test(NAME cli_status_test
    COMMAND ${CMAKE_COMMAND}
      "-DRTCMP_EXECUTABLE=$<TARGET_FILE:rtcmp>"
      "-DRTCMP_RUNTIME_DIRECTORY=${BRLCAD_ROOT}/bin"
      "-DTEST_OUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/cli-status-test"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_status_test.cmake"
    )

  set(rtcmp_identical_build_geometry "${BRLCAD_ROOT}/share/db/cube.g")
  if(EXISTS "${rtcmp_identical_build_geometry}")
    add_test(NAME identical_build_test
      COMMAND ${CMAKE_COMMAND}
        "-DBASELINE_RTCMP=$<TARGET_FILE:rtcmp>"
        "-DCANDIDATE_RTCMP=$<TARGET_FILE:rtcmp>"
        "-DRTCMP_RUNTIME_DIRECTORY=${BRLCAD_ROOT}/bin"
        "-DTEST_OUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/identical-build-test"
        "-DTEST_GEOMETRY=${rtcmp_identical_build_geometry}"
        "-DTEST_OBJECT=all.g"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/identical_build_test.cmake"
      )
  endif()
endif()

# Local Variables:
# tab-width: 8
# mode: cmake
# indent-tabs-mode: t
# End:
# ex: shiftwidth=2 tabstop=8

if(NOT DEFINED TEST_EXECUTABLE)
  message(FATAL_ERROR "TEST_EXECUTABLE is required")
endif()

if(WIN32)
  if(NOT DEFINED RTCMP_RUNTIME_DIRECTORY)
    message(FATAL_ERROR "RTCMP_RUNTIME_DIRECTORY is required on Windows")
  endif()
  set(ENV{PATH} "${RTCMP_RUNTIME_DIRECTORY};$ENV{PATH}")
endif()

execute_process(
  COMMAND "${TEST_EXECUTABLE}" ${TEST_ARGUMENTS}
  RESULT_VARIABLE test_result
  OUTPUT_VARIABLE test_stdout
  ERROR_VARIABLE test_stderr
  )

if(NOT "${test_stdout}" STREQUAL "")
  message("${test_stdout}")
endif()
if(NOT "${test_stderr}" STREQUAL "")
  message("${test_stderr}")
endif()
if(NOT "${test_result}" STREQUAL "0")
  message(FATAL_ERROR "${TEST_EXECUTABLE} exited with ${test_result}")
endif()

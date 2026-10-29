if(NOT DEFINED RTCMP_EXECUTABLE OR NOT DEFINED TEST_OUTPUT_DIRECTORY)
  message(FATAL_ERROR "RTCMP_EXECUTABLE and TEST_OUTPUT_DIRECTORY are required")
endif()

if(WIN32)
  if(NOT DEFINED RTCMP_RUNTIME_DIRECTORY)
    message(FATAL_ERROR "RTCMP_RUNTIME_DIRECTORY is required on Windows")
  endif()
  set(ENV{PATH} "${RTCMP_RUNTIME_DIRECTORY};$ENV{PATH}")
endif()

file(REMOVE_RECURSE "${TEST_OUTPUT_DIRECTORY}")
file(MAKE_DIRECTORY "${TEST_OUTPUT_DIRECTORY}")
set(status_file "${TEST_OUTPUT_DIRECTORY}/usage-status.json")
file(WRITE "${status_file}" "{\"state\":\"stale\"}\n")

execute_process(
  COMMAND "${RTCMP_EXECUTABLE}"
    --output-status "${status_file}"
    --compare baseline.json candidate.json
  RESULT_VARIABLE command_result
  OUTPUT_VARIABLE command_stdout
  ERROR_VARIABLE command_stderr
  )

if(NOT "${command_result}" STREQUAL "2")
  message(FATAL_ERROR
    "expected usage exit 2, got ${command_result}\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

file(READ "${status_file}" status_json)
string(FIND "${status_json}" "\"state\":\"configuration_error\"" state_position)
string(FIND "${status_json}" "\"exit_code\":2" exit_position)
if(state_position EQUAL -1 OR exit_position EQUAL -1)
  message(FATAL_ERROR "usage failure did not replace stale status JSON: ${status_json}")
endif()

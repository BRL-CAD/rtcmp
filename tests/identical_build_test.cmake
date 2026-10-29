if(NOT DEFINED BASELINE_RTCMP OR NOT DEFINED CANDIDATE_RTCMP
    OR NOT DEFINED TEST_OUTPUT_DIRECTORY OR NOT DEFINED TEST_GEOMETRY
    OR NOT DEFINED TEST_OBJECT)
  message(FATAL_ERROR "identical-build test inputs are incomplete")
endif()

if(WIN32)
  if(NOT DEFINED RTCMP_RUNTIME_DIRECTORY)
    message(FATAL_ERROR "RTCMP_RUNTIME_DIRECTORY is required on Windows")
  endif()
  set(ENV{PATH} "${RTCMP_RUNTIME_DIRECTORY};$ENV{PATH}")
endif()

function(run_checked label)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    )
  if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
      "${label} failed with exit ${result}\nstdout:\n${stdout}\nstderr:\n${stderr}")
  endif()
endfunction()

function(run_expected_exit label expected_exit)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    )
  if(NOT "${result}" STREQUAL "${expected_exit}")
    message(FATAL_ERROR
      "${label} returned ${result}, expected ${expected_exit}\nstdout:\n${stdout}\nstderr:\n${stderr}")
  endif()
endfunction()

function(require_no_findings status_file findings_file)
  file(READ "${status_file}" status)
  string(FIND "${status}" "\"state\":\"equivalent\"" equivalent_position)
  string(FIND "${status}" "\"different_rays\":0" zero_position)
  if(equivalent_position EQUAL -1 OR zero_position EQUAL -1)
    message(FATAL_ERROR "comparison did not report zero findings: ${status}")
  endif()
  if(EXISTS "${findings_file}")
    message(FATAL_ERROR "zero-findings comparison left ${findings_file}")
  endif()
endfunction()

file(REMOVE_RECURSE "${TEST_OUTPUT_DIRECTORY}")
file(MAKE_DIRECTORY "${TEST_OUTPUT_DIRECTORY}")

set(rays "${TEST_OUTPUT_DIRECTORY}/canonical.rays")
set(baseline "${TEST_OUTPUT_DIRECTORY}/baseline.json")
set(candidate "${TEST_OUTPUT_DIRECTORY}/candidate.json")
set(status "${TEST_OUTPUT_DIRECTORY}/comparison.status.json")
set(findings "${TEST_OUTPUT_DIRECTORY}/findings.rays")

run_checked("baseline generation"
  "${BASELINE_RTCMP}" -d -n 4 --rays-per-view 1
  --output-rays "${rays}" --output-json "${baseline}"
  --output-status "${TEST_OUTPUT_DIRECTORY}/baseline.status.json"
  "${TEST_GEOMETRY}" "${TEST_OBJECT}")
run_checked("candidate replay"
  "${CANDIDATE_RTCMP}" -d -n 4 --input-rays "${rays}"
  --output-json "${candidate}"
  --output-status "${TEST_OUTPUT_DIRECTORY}/candidate.status.json"
  "${TEST_GEOMETRY}" "${TEST_OBJECT}")
run_checked("strict identical-build comparison"
  "${BASELINE_RTCMP}" -c -t 0 --input-rays "${rays}"
  --grazing-geometry "${TEST_GEOMETRY}" --grazing-object "${TEST_OBJECT}"
  --output-nirt "${findings}" --output-status "${status}"
  "${baseline}" "${candidate}")
require_no_findings("${status}" "${findings}")

set(tie_baseline "${TEST_OUTPUT_DIRECTORY}/tie-baseline.json")
set(tie_candidate "${TEST_OUTPUT_DIRECTORY}/tie-candidate.json")
set(tie_status "${TEST_OUTPUT_DIRECTORY}/tie-comparison.status.json")
set(tie_findings "${TEST_OUTPUT_DIRECTORY}/tie-findings.rays")
run_checked("TIE baseline generation"
  "${BASELINE_RTCMP}" -d --enable-tie -n 4 --input-rays "${rays}"
  --output-json "${tie_baseline}"
  --output-status "${TEST_OUTPUT_DIRECTORY}/tie-baseline.status.json"
  "${TEST_GEOMETRY}" "${TEST_OBJECT}")
run_checked("TIE candidate replay"
  "${CANDIDATE_RTCMP}" -d --enable-tie -n 4 --input-rays "${rays}"
  --output-json "${tie_candidate}"
  --output-status "${TEST_OUTPUT_DIRECTORY}/tie-candidate.status.json"
  "${TEST_GEOMETRY}" "${TEST_OBJECT}")
run_checked("TIE identical-build comparison"
  "${BASELINE_RTCMP}" -c -t 0 --input-rays "${rays}"
  --output-nirt "${tie_findings}" --output-status "${tie_status}"
  "${tie_baseline}" "${tie_candidate}")
require_no_findings("${tie_status}" "${tie_findings}")

set(primitive_baseline "${TEST_OUTPUT_DIRECTORY}/primitive-baseline.json")
set(primitive_candidate "${TEST_OUTPUT_DIRECTORY}/primitive-candidate.json")
set(primitive_status "${TEST_OUTPUT_DIRECTORY}/primitive-comparison.status.json")
set(primitive_findings "${TEST_OUTPUT_DIRECTORY}/primitive-findings.rays")

run_checked("primitive baseline generation"
  "${BASELINE_RTCMP}" -d -n 4 --primitive-hits --input-rays "${rays}"
  --output-json "${primitive_baseline}"
  --output-status "${TEST_OUTPUT_DIRECTORY}/primitive-baseline.status.json"
  "${TEST_GEOMETRY}" "${TEST_OBJECT}")
run_checked("primitive candidate replay"
  "${CANDIDATE_RTCMP}" -d -n 4 --primitive-hits --input-rays "${rays}"
  --output-json "${primitive_candidate}"
  --output-status "${TEST_OUTPUT_DIRECTORY}/primitive-candidate.status.json"
  "${TEST_GEOMETRY}" "${TEST_OBJECT}")
run_checked("primitive identical-build comparison"
  "${BASELINE_RTCMP}" -c -t 0 --input-rays "${rays}"
  --output-nirt "${primitive_findings}" --output-status "${primitive_status}"
  "${primitive_baseline}" "${primitive_candidate}")
require_no_findings("${primitive_status}" "${primitive_findings}")

file(READ "${primitive_baseline}" primitive_contents LIMIT 1048576)
string(FIND "${primitive_contents}" "\"segments\":[" segments_position)
if(segments_position EQUAL -1)
  message(FATAL_ERROR "primitive capture did not emit the segment schema")
endif()

set(invalid_rays "${TEST_OUTPUT_DIRECTORY}/invalid.rays")
set(stale_result "${TEST_OUTPUT_DIRECTORY}/stale-result.json")
file(WRITE "${invalid_rays}" "invalid rayset\n")
file(WRITE "${stale_result}" "stale result\n")
run_expected_exit("invalid rayset generation" 5
  "${BASELINE_RTCMP}" -d --input-rays "${invalid_rays}"
  --output-json "${stale_result}"
  --output-status "${TEST_OUTPUT_DIRECTORY}/invalid.status.json"
  "${TEST_GEOMETRY}" "${TEST_OBJECT}")
if(EXISTS "${stale_result}")
  message(FATAL_ERROR "failed generation retained a stale result file")
endif()

file(GLOB temporary_outputs "${TEST_OUTPUT_DIRECTORY}/*.tmp")
if(temporary_outputs)
  message(FATAL_ERROR "generation left temporary output files: ${temporary_outputs}")
endif()

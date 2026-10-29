#!/usr/bin/env bash
set -euo pipefail

source_dir="${1:?source directory is required}"
source "$source_dir/tests.sh"

test_directory="$(mktemp -d "${TMPDIR:-/tmp}/rtcmp-harness-cleanup.XXXXXX")"
cleanup_test_directory() {
    if [[ -n "$test_directory" && -d "$test_directory" ]]; then
	rm -rf -- "$test_directory"
    fi
}
trap cleanup_test_directory EXIT

failures=0
check_artifact_policy() {
    local status="$1"
    local expect_preserved="$2"
    local case_directory="$test_directory/$status"
    local baseline_json="$case_directory/baseline.json"
    local candidate_json="$case_directory/candidate.json"
    local rays="$case_directory/canonical.rays"

    mkdir -p "$case_directory"
    : > "$baseline_json"
    : > "$candidate_json"
    : > "$rays"

    cleanup_correctness_artifacts "$status" "$baseline_json" "$candidate_json" "$rays"

    local artifact
    for artifact in "$baseline_json" "$candidate_json" "$rays"; do
	if [[ "$expect_preserved" == "1" && ! -f "$artifact" ]]; then
	    echo "FAIL: $status removed $artifact" >&2
	    failures=$((failures + 1))
	elif [[ "$expect_preserved" == "0" && -e "$artifact" ]]; then
	    echo "FAIL: $status preserved $artifact" >&2
	    failures=$((failures + 1))
	fi
    done
}

CLEANUP_OUTPUT_ARTIFACTS=1
check_artifact_policy PASS 0
check_artifact_policy FAIL 1
check_artifact_policy ERROR 1

if ((failures != 0)); then
    echo "$failures harness cleanup test(s) failed" >&2
    exit 1
fi

echo "harness cleanup tests passed"

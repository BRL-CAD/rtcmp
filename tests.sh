#!/bin/bash
set -euo pipefail
#set -x

########################################
# 0) CONFIG
# overwrite by 'export' or supplying command line
#   > THING_TO_CHANGE="change" OTHER_THING="this" ./run.sh
########################################

# paths to both rtcmp builds
CMD1="${CMD1:-/pathto/rtcmp/brl_main-build/rtcmp.exe}"
CMD2="${CMD2:-/pathto/rtcmp/brl_rel-build/rtcmp.exe}"

# use MGED so we can get investigate and summarize each .g
MGED="${MGED:-/path/to/mged.exe}"

# dirs to collect models from (space-separated list - NOTE: this means paths CANNOT have spaces)
#   - If MODEL_DIRS are set: gather all .g files under them (non-recursive)
#   - Else: gather all .g files in current directory
MODEL_DIRS="${MODEL_DIRS:-./bots_only ./breps_only ./primitives_only ./mixed_bag}"
HIER_DEPTH="${HIER_DEPTH:-0}"

# output directory for artifacts
OUTDIR="${OUTDIR:-rtcmp_out}"
# delete non-essential output as we go to not consume the disk
CLEANUP_OUTPUT_ARTIFACTS="${CLEANUP_OUTPUT_ARTIFACTS:-1}"

# comparison tolerance
#   start at max tolerance, if we get no differences we're good; otherwise loosen to see if we can get a pass
TOLS_LIST="${TOLS_LIST:-1e-15 1e-12 1e-1}"
DO_ALL_TOLS="${DO_ALL_TOLS:-0}"     # don't stop at first failing tol

# do performance tests?
DO_PERF="${DO_PERF:-1}"
PERF_SECONDS="${PERF_SECONDS:-3}"               # use 3 sec for perf runs
PERF_MAX_MEMORY="${PERF_MAX_MEMORY:-0}"         # dont limit memory for perf

# maximize cpu usage by default
NUM_CPUS="${NUM_CPUS:-0}"

RAYS_PER_VIEW="${RAYS_PER_VIEW:-100000}"    # does not like 1eXX notation

# LOG_LEVEL:
#   0 = normal (default): current processing (file / component) + errors
#   1 = verbose: more progress detail
#   2 = debug: very chatty
LOG_LEVEL="${LOG_LEVEL:-0}"

########################################
# 1) VALIDATION/ HELPERS
########################################

log() {
    local level msg

    case "${1:-}" in
        GENERIC|VERBOSE|DEBUG|ERROR)
            level="$1"
            shift
            ;;
        *)
            level="GENERIC"
            ;;
    esac

    msg="$*"

    case "$level" in
        ERROR)
            echo "ERROR: $msg" >&2
            ;;
        GENERIC)
            echo "$msg" >&2
            ;;
        VERBOSE)
            if (( LOG_LEVEL >= 1 )); then
                echo "$msg" >&2
            fi
            ;;
        DEBUG)
            if (( LOG_LEVEL >= 2 )); then
                echo "[DEBUG] $msg" >&2
            fi
            ;;
    esac
}
die() { log ERROR "$*"; exit 1; }

safe_tag() {
    # create a filesystem-safe tag for output filenames
    local s="$1"
    echo "$s" | sed -e 's/[\/\\:*?"<>| ]\+/_/g'
}

need_exec() {
    local p="$1"
    [[ -x "$p" ]] || die "Missing or not executable: $p"
}

need_file() {
    local p="$1"
    [[ -f "$p" ]] || die "Missing file: $p"
}

check_prereqs() {
    # TODO: check for / copy all .dll if this is windows
    # we need two rtcmp builds
    need_exec "$CMD1"
    need_exec "$CMD2"

    # we need mged for tops gathering
    need_exec "$MGED"

    # we need an output dir
    mkdir -p "$OUTDIR"
}

abs_path() {
    local p="$1"
    
    if command -v realpath >/dev/null 2>&1; then
	realpath "$p"
    else
	(cd "$p" && pwd -P)
    fi
}

########################################
# 2) .g TEST SUITE PREP
########################################

discover_g_files() {
    local -a dirs=()

    # assume a space means we have a list of dirs
    if [[ -n "${MODEL_DIRS//[[:space:]]/}" ]]; then
        dirs=( $MODEL_DIRS )
    else
        dirs=( "." )
    fi

    log DEBUG "discover_g_files: dirs='${dirs[*]}' pwd='$(pwd)'"

    local d abs_d
    for d in "${dirs[@]}"; do
        [[ -n "${d//[[:space:]]/}" ]] || continue
        [[ -d "$d" ]] || die "MODEL_DIRS entry is not a directory: $d"

	abs_d="$(abs_path  "$d")"

        # non-recursive .g discovery
        find "$abs_d" -maxdepth 1 -type f -name '*.g' -print
    done \
    | awk 'NF' \
    | sort -u
}

get_geom_metrics() {
    local gfile="$1"
    local comp="$2"

    # MGED prints to stderr, so capture stderr into stdout.
    # Also discard stdout just in case.
    local s
    s="$("$MGED" -c "$gfile" summary "$comp" 2>&1 >/dev/null | tr -d '\r' | tr '\n' ' ' | tr -s ' ')"

    local bots=0 bot_faces=0 breps=0 prims=0

    # Example tokens:
    #   "0 bots"
    #   "1 bots (192 faces)"
    #   "0 breps"
    #   "2 BRL-CAD primitives"
    if [[ $s =~ ([0-9]+)[[:space:]]+bots ]]; then
        bots="${BASH_REMATCH[1]}"
    fi
    if [[ $s =~ bots[[:space:]]*\(([0-9]+)[[:space:]]+faces\) ]]; then
        bot_faces="${BASH_REMATCH[1]}"
    fi
    if [[ $s =~ ([0-9]+)[[:space:]]+breps ]]; then
        breps="${BASH_REMATCH[1]}"
    fi
    if [[ $s =~ ([0-9]+)[[:space:]]+BRL-CAD[[:space:]]+primitives ]]; then
        prims="${BASH_REMATCH[1]}"
    fi

    echo "${bots}|${bot_faces}|${breps}|${prims}"
}

get_components() {
    local gfile="$1"
    local depth_arg="-maxdepth $HIER_DEPTH"

    log DEBUG "MGED object search: file='$gfile' depth_arg='$depth_arg'"

    {
        printf 'search . %s -exec if {[llength [search /{} -type half]] == 0} {puts {}} ";"\n' "$depth_arg"
        printf 'q\n'
    } | "$MGED" -c "$gfile" 2>&1 \
      | tr -d '\r' \
      | awk 'NF && $0 !~ /^mged>/ && $0 !~ /^BRL-CAD/'
}

########################################
# 3) COMPARISON TESTS
########################################

# Run the two difference-test passes: "rtcmp -d file comp"
#   1) CMD1 generates rays + json1
#   2) CMD2 reuses rays + json2
run_generate_jsons() {
    local gfile="$1"
    local comp="$2"
    local tag="$3"

    local json1="$OUTDIR/${tag}.json1"
    local json2="$OUTDIR/${tag}.json2"
    local rays="$OUTDIR/${tag}.rays"

    local gen_start gen_end gen_time
    gen_start="$(date +%s.%N)"

    "$CMD1" --rays-per-view "$RAYS_PER_VIEW" -n "$NUM_CPUS" --output-json "$json1" --output-rays "$rays" -d "$gfile" "$comp"
    "$CMD2" --rays-per-view "$RAYS_PER_VIEW" -n "$NUM_CPUS" --output-json "$json2" --input-rays  "$rays" -d "$gfile" "$comp"

    gen_end="$(date +%s.%N)"
    gen_time="$(awk -v e="$gen_end" -v s="$gen_start" 'BEGIN{print e - s}')"

    echo "$json1|$json2|$rays|$gen_time"
}

# Parse rtcmp compare output: "rtcmp -c .json1 .json2"
#   - PASS iff "No differences found" (THIS MUST MATCH rtcmp OUTPUT)
parse_compare_output() {
    local logfile="$1"
    local MATCH_STRING="No differences found"
    local status="FAIL"

    if grep -Fq "$MATCH_STRING" "$logfile"; then
	status="PASS"
    fi

    # clean up uninteresting output
    if [[ "$status" == "PASS" && "$CLEANUP_OUTPUT_ARTIFACTS" == "1" ]]; then
        rm -f "$logfile"
    fi

    echo "$status"
}

# Run compare, capturing logs. Returns PASS/FAIL and writes artifacts
run_compare_logged() {
    local json1="$1"
    local json2="$2"
    local tag="$3"
    local tol="$4"

    local nirt="$OUTDIR/${tag}.t${tol}.nirt"
    local clog="$OUTDIR/${tag}.t${tol}.compare.log"

    "$CMD1" --output-nirt "$nirt" -t "$tol" -c "$json1" "$json2" >"$clog" 2>&1

    parse_compare_output "$clog"
}

# shrink tolerance until we fail (or get to a minimum)
# returns: "PASS|best_tol" or "FAIL|fail_tol"
comp_at_stepping_tols() {
    local json1="$1"
    local json2="$2"
    local tag="$3"

    local tol status=""
    local last_fail_tol=""

    for tol in $TOLS_LIST; do
        status="$(run_compare_logged "$json1" "$json2" "$tag" "$tol")"

        if [[ "$status" == "PASS" ]]; then
            if [[ -z "$last_fail_tol" ]]; then
                echo "PASS|$tol"
            else
                echo "FAIL|$last_fail_tol"
            fi
            return 0
        fi

        last_fail_tol="$tol"

        log "              FAIL at tol=$tol"
        log "                diff log : $OUTDIR/${tag}.t${tol}.compare.log"
        log "                diff nirt: $OUTDIR/${tag}.t${tol}.nirt"
    done

    echo "FAIL|$last_fail_tol"
}

get_num_rays() {
    local raysFile="$1"

    [[ -f "$raysFile" ]] || { echo ""; return; }

    # First line looks like:
    # **rays fired for <path> [num_rays]**
    sed -n '1p' "$raysFile" | sed -n 's/.*\[\([0-9]\+\)\].*/\1/p'
}

########################################
# 4) PERFORMANCE TESTS
########################################

parse_perf_output() {
    local file="$1"

    local rps
    rps="$(tr -d '\r' <"$file" | awk '/^[[:space:]]*Rays\/sec \[wall\][[:space:]]*\(/ {print $NF; exit}' || true)"

    # only case about rays per sec
    echo "${rps:-}"
}

calc_perf_metrics() {
    # Inputs: rps1 rps2
    # Outputs: rps_ratio
    #
    # Throughput metric: higher is better.
    local rps1="$1"
    local rps2="$2"

    awk -v r1="$rps1" -v r2="$rps2" '
      function isnum(x) { return (x ~ /^([0-9]*\.)?[0-9]+([eE][-+]?[0-9]+)?$/) }
      BEGIN {
        if (!isnum(r1) || !isnum(r2) || (r1+0) == 0 || (r2+0) == 0) {
          print "|"
          exit
        }

        if ((r2+0) >= (r1+0)) {
          ratio = r2 / r1;       # faster => positive
        } else {
          ratio = -(r1 / r2);    # slower => negative
        }

        printf "%.6g", ratio;
      }
    '
}

run_perf_test() {
    local gfile="$1"
    local comp="$2"
    local tag="$3"

    local out1="$OUTDIR/${tag}.perf1.txt"
    local out2="$OUTDIR/${tag}.perf2.txt"

    "$CMD1" --perf-seconds "$PERF_SECONDS" --perf-max_memory "$PERF_MAX_MEMORY" -n "$NUM_CPUS" -p "$gfile" "$comp" >"$out1" 2>&1
    "$CMD2" --perf-seconds "$PERF_SECONDS" --perf-max_memory "$PERF_MAX_MEMORY" -n "$NUM_CPUS" -p "$gfile" "$comp" >"$out2" 2>&1

    local rps1 rps2 metrics rps_ratio
    rps1="$(parse_perf_output "$out1")"
    rps2="$(parse_perf_output "$out2")"

    metrics="$(calc_perf_metrics "$rps1" "$rps2")"
    rps_ratio="$(echo "$metrics" | cut -d'|' -f1)"

    if [[ "$CLEANUP_OUTPUT_ARTIFACTS" == "1" ]]; then
        rm -f "$out1" "$out2"
    fi

    echo "${rps1}|${rps2}|${rps_ratio}"
}

########################################
# 5) MAIN
########################################

main() {
    check_prereqs

    # output findings to csv
    local summary_csv="$OUTDIR/summary.csv"
    echo "file,component,tag,bots,bot_faces,breps,brlcad_prims,num_comp_rays,compare_status,comp_status_tol,perf1_rays_per_sec_wall,perf2_rays_per_sec_wall,rays_per_sec_ratio" >"$summary_csv"

    # gather .g files
    mapfile -t gfiles < <(discover_g_files || true)
    [[ "${#gfiles[@]}" -gt 0 ]] || die "No .g files found."

    # iterate for each file
    for gfile in "${gfiles[@]}"; do
        [[ -n "${gfile//[[:space:]]/}" ]] || continue
        need_file "$gfile"

        log ""      # visual separation
        log "==> processing: $gfile"

        # gather components for this file
        mapfile -t comps < <(get_components "$gfile" || true)
        [[ "${#comps[@]}" -gt 0 ]] || { log "    (no tops found)"; continue; }

        # run rtcmp for each comp
        for comp in "${comps[@]}"; do
            [[ -n "${comp//[[:space:]]/}" ]] || continue

            # build a unique file+comp tag for output artifacts
            local base tag
            base="$(basename "$gfile")"
            tag="$(safe_tag "${base}.${comp}")"
            log "      $comp (tag: $tag)"

            # summarize what we're running on
            local geom_info bots bot_faces breps prims
            geom_info="$(get_geom_metrics "$gfile" "$comp")"
            bots="$(echo "$geom_info" | cut -d'|' -f1)"
            bot_faces="$(echo "$geom_info" | cut -d'|' -f2)"
            breps="$(echo "$geom_info" | cut -d'|' -f3)"
            prims="$(echo "$geom_info" | cut -d'|' -f4)"
            log VERBOSE "        geom: bots=$bots faces=$bot_faces breps=$breps prims=$prims"

            # do comp generation
            log VERBOSE "        running comparison tests..."
            local gen_info json1 json2 rays gen_time
            gen_info="$(run_generate_jsons "$gfile" "$comp" "$tag")"
            json1="$(echo "$gen_info" | cut -d'|' -f1)"
            json2="$(echo "$gen_info" | cut -d'|' -f2)"
            rays="$(echo "$gen_info" | cut -d'|' -f3)"
            gen_time="$(echo "$gen_info" | cut -d'|' -f4)"

            log VERBOSE "            JSON's generated in $gen_time seconds"
            log VERBOSE "            json1: $json1"
            log VERBOSE "            json2: $json2"
            log VERBOSE "            rays : $rays"

            # do actual comp
            local tol_start tol_end tol_time outcome status pass_tol
            tol_start="$(date +%s.%N)"

            outcome="$(comp_at_stepping_tols "$json1" "$json2" "$tag")"
            status="$(echo "$outcome" | cut -d'|' -f1)"
            pass_tol="$(echo "$outcome" | cut -d'|' -f2)"

            tol_end="$(date +%s.%N)"
            tol_time="$(awk -v e="$tol_end" -v s="$tol_start" 'BEGIN{print e - s}')"

            local num_rays
            num_rays="$(get_num_rays "$rays")"

            log VERBOSE "            compare: $status (tol=$pass_tol) in $tol_time seconds"

            # cleanup comp artifacts
            if [[ "$CLEANUP_OUTPUT_ARTIFACTS" == "1" ]]; then
                rm -f "$json1" "$json2" "$rays"
            fi

            # do perf tests
            local perf_rps1 perf_rps2 rps_ratio
            perf_rps1=""; perf_rps2=""; rps_ratio=""

            if [[ "$DO_PERF" == "1" ]]; then
                log VERBOSE "        running perf tests..."
                local perf_info
                perf_info="$(run_perf_test "$gfile" "$comp" "$tag")"

                perf_rps1="$(echo "$perf_info" | cut -d'|' -f1)"
                perf_rps2="$(echo "$perf_info" | cut -d'|' -f2)"
                rps_ratio="$(echo "$perf_info" | cut -d'|' -f3)"

                log VERBOSE "            throughput delta: ${rps_ratio}x"
            fi
            
            # write to our csv
            echo "\"$gfile\",\"$comp\",\"$tag\",$bots,$bot_faces,$breps,$prims,$num_rays,$status,$pass_tol,$perf_rps1,$perf_rps2,$rps_ratio" >>"$summary_csv"
        done
    done

    # print our config to the footer of the csv
    {
      echo ""
      echo "#CONFIG,key,value"
      echo "#CONFIG,CMD1,\"$CMD1\""
      echo "#CONFIG,CMD2,\"$CMD2\""
      # make sure we extract absolute paths of model dir(s)
      if [[ -n "${MODEL_DIRS//[[:space:]]/}" ]]; then
          for d in $MODEL_DIRS; do
              echo "#CONFIG,MODEL_DIR,\"$(abs_path "$d")\""
          done
      else
          echo "#CONFIG,MODEL_DIR,\"$(pwd -P)\""
      fi

      echo "#CONFIG,TOLS_LIST,\"$TOLS_LIST\""
      echo "#CONFIG,NUM_CPUS,\"$NUM_CPUS\""
      echo "#CONFIG,RAYS_PER_VIEW,\"$RAYS_PER_VIEW\""
      echo "#CONFIG,PERF_SECONDS,\"$PERF_SECONDS\""
      echo "#CONFIG,PERF_MAX_MEMORY,\"$PERF_MAX_MEMORY\""
    } >>"$summary_csv"

    # we're done; final log
    log ""
    log "==> Done."
    log "    Config used:"
    log "      CMD1         : $CMD1"
    log "      CMD2         : $CMD2"
    log "      MODEL_DIRS   :"
    if [[ -n "${MODEL_DIRS//[[:space:]]/}" ]]; then
        for d in $MODEL_DIRS; do
            log "        - $(abs_path "$d")"
        done
    else
        log "        - $(pwd -P)"
    fi
    log "      TOLS_LIST    : $TOLS_LIST"
    log "      NUM_CPUS     : $NUM_CPUS"
    log "      RAYS_PER_VIEW: $RAYS_PER_VIEW"
    log "      PERF_SECONDS : $PERF_SECONDS"
    log "      PERF_MAX_MEM : $PERF_MAX_MEMORY"
    log "   Summary: $summary_csv"
}

# and away we go
main

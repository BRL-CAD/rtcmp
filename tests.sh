#!/bin/bash
set -euo pipefail
#set -x

########################################
# 0) CONFIG
# overwrite by 'export' or supplying command line
#   > THING_TO_CHANGE="change" OTHER_THING="this" ./run.sh
########################################

# paths to both rtcmp builds
CMD1=/pathto/rtcmp/brl_main-build/rtcmp.exe
CMD2=/pathto/rtcmp/brl_rel-build/rtcmp.exe

# use MGED so we can get investigate and summarize each .g
MGED=/path/to/mged.exe

# models to run on
#   - If MODEL_DIR is set: gather all .g files under it (non-recursive)
#   - Else: gather all .g files in current directory
MODEL_DIR="${MODEL_DIR:-}"

# output directory for artifacts
OUTDIR="${OUTDIR:-rtcmp_out}"
mkdir -p "$OUTDIR"

# comparison tolerance
#   start at a loose tolerance, if we get no differences; tighten until we do
TOLS_LIST="${TOLS_LIST:-1e-1 1e-8 1e-12 1e-15}"
DO_ALL_TOLS="${DO_ALL_TOLS:-0}"     # don't stop at first failing tol

# do performance tests?
DO_PERF="${DO_PERF:-1}"

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
    # we need two rtcmp builds
    need_exec "$CMD1"
    need_exec "$CMD2"

    # we need mged for tops gathering
    need_exec "$MGED"

    # we need an output dir
    mkdir -p "$OUTDIR"
}

########################################
# 2) .g TEST SUITE PREP
########################################

discover_g_files() {
    local dir="${1:-}"

    log DEBUG "discover_g_files: MODEL_DIR='${MODEL_DIR:-}' arg_dir='${dir}' pwd='$(pwd)'"
    #log DEBUG "listing dir:"; ls -la >&2

    # non-recusive find all .g files (either in specified dir or curr)
    if [[ -n "$dir" ]]; then
        [[ -d "$dir" ]] || die "MODEL_DIR is not a directory: $dir"
        find "$dir" -maxdepth 1 -type f -name '*.g' -print
    else
        find . -maxdepth 1 -type f -name '*.g' -print
    fi
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

get_tops() {
    local gfile="$1"

    # get our tops objects with search so we can avoid problem geometry (like halfspaces)
    # Feed MGED via stdin to avoid shell quoting issues
    # Parse ONLY stdout as tops; keep stderr on the console
    "$MGED" -c "$gfile" <<'EOF' 2> >(cat >&2) | tr -d '\r' | awk 'NF'
search . -maxdepth 0 -exec {if {[llength [search /{} -type half]] == 0} {puts {}}} ";"
q
EOF
}

build_pairs() {
    while IFS= read -r gfile; do
        [[ -n "${gfile//[[:space:]]/}" ]] || continue

        while IFS= read -r top; do
            [[ -n "${top//[[:space:]]/}" ]] || continue
            printf '%s %s\n' "$gfile" "$top"
        done < <(get_tops "$gfile" || true)

    done < <(discover_g_files "$MODEL_DIR" || true)
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

    if grep -Fq "$MATCH_STRING" "$logfile"; then
        echo "PASS"
    else
        echo "FAIL"
    fi
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

    local tol last_tol="" status=""
    local first_fail_tol=""   # first failing tol encountered (in list order)

    for tol in $TOLS_LIST; do
        last_tol="$tol"
        status="$(run_compare_logged "$json1" "$json2" "$tag" "$tol")"

        if [[ "$status" == "FAIL" ]]; then
            # log every failure
            log "              FAIL at tol=$tol"
            log "                diff log : $OUTDIR/${tag}.t${tol}.compare.log"
            log "                diff nirt: $OUTDIR/${tag}.t${tol}.nirt"

            # record first failure only
            if [[ -z "$first_fail_tol" ]]; then
                first_fail_tol="$tol"
            fi

            # stop on first failure unless "do all tols" requested
            if [ "$DO_ALL_TOLS" -eq 0 ]; then
                echo "FAIL|$tol"
                return 0
            fi
        fi
    done

    # If any failures occurred (and we ran all tols), return the first failing tol
    if [[ -n "$first_fail_tol" ]]; then
        echo "FAIL|$first_fail_tol"
    else
        # passed all levels
        echo "PASS|$last_tol"
    fi
}

########################################
# 4) PERFORMANCE TESTS
########################################

parse_perf_output() {
    local file="$1"

    # ignore ERROR lines by simply matching the time lines
    local wall cpu
    wall="$(tr -d '\r' <"$file" | awk '/^[[:space:]]*Wall clock time[[:space:]]*\(/ {print $NF; exit}' || true)"
    cpu="$(tr -d '\r' <"$file" | awk '/^[[:space:]]*CPU time[[:space:]]*\(/        {print $NF; exit}' || true)"

    # print: wall|cpu (empty if not found)
    echo "${wall:-}|${cpu:-}"
}

calc_perf_metrics() {
    # Inputs: wall1 cpu1 wall2 cpu2
    # Outputs: wall_ratio|wall_pct|cpu_ratio|cpu_pct
    #
    # Signed ratio (baseline=first run):
    #   - if t2 < t1 (second faster):  + (t1 / t2)
    #   - if t2 > t1 (second slower):  - (t2 / t1)
    #   - if equal: +1
    #
    # Percent change (positive=faster, negative=slower), relative to first:
    #   ((t1 - t2) / t1) * 100

    local w1="$1" c1="$2" w2="$3" c2="$4"

    awk -v w1="$w1" -v c1="$c1" -v w2="$w2" -v c2="$c2" '
      function isnum(x) { return (x ~ /^([0-9]*\.)?[0-9]+([eE][-+]?[0-9]+)?$/) }
      function signed_ratio(t1, t2) {
        # returns "" if invalid
        if (!isnum(t1) || !isnum(t2) || (t1+0) == 0 || (t2+0) == 0) return "";
        if ((t2+0) < (t1+0)) return (t1 / t2);      # faster => positive
        if ((t2+0) > (t1+0)) return -(t2 / t1);     # slower => negative (magnitude > 1)
        return 1.0;
      }
      function pct_change(t1, t2) {
        if (!isnum(t1) || !isnum(t2) || (t1+0) == 0) return "";
        return ((t1 - t2) / t1) * 100.0;
      }
      BEGIN {
        wall_r=""; wall_pct="";
        cpu_r="";  cpu_pct="";

        wall_r   = signed_ratio(w1, w2);
        wall_pct = pct_change(w1, w2);

        cpu_r    = signed_ratio(c1, c2);
        cpu_pct  = pct_change(c1, c2);

        if (wall_r   != "") wall_r   = sprintf("%.6g", wall_r);
        if (wall_pct != "") wall_pct = sprintf("%.3f", wall_pct);
        if (cpu_r    != "") cpu_r    = sprintf("%.6g", cpu_r);
        if (cpu_pct  != "") cpu_pct  = sprintf("%.3f", cpu_pct);

        print wall_r "|" wall_pct "|" cpu_r "|" cpu_pct;
      }
    '
}

run_perf_test() {
    local gfile="$1"
    local comp="$2"
    local tag="$3"

    # rtcmp perf mode doesn't store results; capture stdout
    local out1="$OUTDIR/${tag}.perf1.txt"
    local out2="$OUTDIR/${tag}.perf2.txt"

    "$CMD1" --rays-per-view "$RAYS_PER_VIEW" -n "$NUM_CPUS" -p "$gfile" "$comp" >"$out1" 2>&1
    "$CMD2" --rays-per-view "$RAYS_PER_VIEW" -n "$NUM_CPUS" -p "$gfile" "$comp" >"$out2" 2>&1

    local p1 p2 wall1 cpu1 wall2 cpu2
    p1="$(parse_perf_output "$out1")"
    p2="$(parse_perf_output "$out2")"
    wall1="$(echo "$p1" | cut -d'|' -f1)"; cpu1="$(echo "$p1" | cut -d'|' -f2)"
    wall2="$(echo "$p2" | cut -d'|' -f1)"; cpu2="$(echo "$p2" | cut -d'|' -f2)"

    local metrics wall_speedup wall_speedup_pct cpu_speedup cpu_speedup_pct
    metrics="$(calc_perf_metrics "$wall1" "$cpu1" "$wall2" "$cpu2")"
    wall_speedup="$(echo "$metrics" | cut -d'|' -f1)"
    wall_speedup_pct="$(echo "$metrics" | cut -d'|' -f2)"
    cpu_speedup="$(echo "$metrics" | cut -d'|' -f3)"
    cpu_speedup_pct="$(echo "$metrics" | cut -d'|' -f4)"

    echo "${wall1}|${cpu1}|${wall2}|${cpu2}|${wall_speedup}|${wall_speedup_pct}|${cpu_speedup}|${cpu_speedup_pct}"
}

########################################
# 5) MAIN
########################################

main() {
    check_prereqs

    # output findings to csv
    local summary_csv="$OUTDIR/summary.csv"
    echo "file,component,tag,bots,bot_faces,breps,brlcad_prims,compare_status,pass_tol,perf1_wall_s,perf1_cpu_s,perf2_wall_s,perf2_cpu_s,wall_speedup_signed,wall_speedup_pct,cpu_speedup_signed,cpu_speedup_pct" >"$summary_csv"

    # gather .g files
    mapfile -t gfiles < <(discover_g_files "$MODEL_DIR" || true)
    [[ "${#gfiles[@]}" -gt 0 ]] || die "No .g files found."

    # iterate for each file
    for gfile in "${gfiles[@]}"; do
        [[ -n "${gfile//[[:space:]]/}" ]] || continue
        need_file "$gfile"

        log ""      # visual separation
        log "==> processing: $gfile"

        # gather components for this file
        mapfile -t comps < <(get_tops "$gfile" || true)
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

            log VERBOSE "            compare: $status (tol=$pass_tol) in $tol_time seconds"

            # do perf tests
            local perf_wall1 perf_cpu1 perf_wall2 perf_cpu2 wall_speedup wall_speedup_pct cpu_speedup cpu_speedup_pct
            perf_wall1=""; perf_cpu1=""; perf_wall2=""; perf_cpu2=""
            wall_speedup=""; wall_speedup_pct=""; cpu_speedup=""; cpu_speedup_pct=""

            if [[ "$DO_PERF" == "1" ]]; then
                log VERBOSE "        running perf tests..."
                local perf_info
                perf_info="$(run_perf_test "$gfile" "$comp" "$tag")"
                perf_wall1="$(echo "$perf_info" | cut -d'|' -f1)"
                perf_cpu1="$(echo "$perf_info" | cut -d'|' -f2)"
                perf_wall2="$(echo "$perf_info" | cut -d'|' -f3)"
                perf_cpu2="$(echo "$perf_info" | cut -d'|' -f4)"
                wall_speedup="$(echo "$perf_info" | cut -d'|' -f5)"
                wall_speedup_pct="$(echo "$perf_info" | cut -d'|' -f6)"
                cpu_speedup="$(echo "$perf_info" | cut -d'|' -f7)"
                cpu_speedup_pct="$(echo "$perf_info" | cut -d'|' -f8)"

                log VERBOSE "            wall delta: ${wall_speedup}x ($wall_speedup_pct%)"
                log VERBOSE "            cpu  delta: ${cpu_speedup}x ($cpu_speedup_pct%)"
            fi
            
            # write to our csv
            echo "\"$gfile\",\"$comp\",\"$tag\",$bots,$bot_faces,$breps,$prims,$status,$pass_tol,$perf_wall1,$perf_cpu1,$perf_wall2,$perf_cpu2,$wall_speedup,$wall_speedup_pct,$cpu_speedup,$cpu_speedup_pct" >>"$summary_csv"
        done
    done

    # print our config to the footer of the csv
    {
      echo ""
      echo "#CONFIG,key,value"
      echo "#CONFIG,CMD1,\"$CMD1\""
      echo "#CONFIG,CMD2,\"$CMD2\""
      echo "#CONFIG,MODEL_DIR,\"${MODEL_DIR:-.}\""
      echo "#CONFIG,TOLS_LIST,\"$TOLS_LIST\""
      echo "#CONFIG,NUM_CPUS,\"$NUM_CPUS\""
      echo "#CONFIG,RAYS_PER_VIEW,\"$RAYS_PER_VIEW\""
    } >>"$summary_csv"

    # we're done; final log
    log ""
    log "==> Done."
    log "    Config used:"
    log "      CMD1         : $CMD1"
    log "      CMD2         : $CMD2"
    log "      MODEL_DIR    : ${MODEL_DIR:-.}"
    log "      TOLS_LIST    : $TOLS_LIST"
    log "      NUM_CPUS     : $NUM_CPUS"
    log "      RAYS_PER_VIEW: $RAYS_PER_VIEW"
    log "   Summary: $summary_csv"
}

# and away we go
main "$@"

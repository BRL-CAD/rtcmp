#!/usr/bin/env bash
set -euo pipefail

########################################
# 0) CONFIG
# overwrite by 'export' or supplying command line
#   > THING_TO_CHANGE="change" OTHER_THING="this" ./run.sh
########################################

CMD1="${CMD1:-}"
CMD2="${CMD2:-}"
MGED="${MGED:-}"

MODEL_DIRS="${MODEL_DIRS:-.}"
OUTDIR="${OUTDIR:-rtcmp_perf_out}"
OUTCSV="${OUTCSV:-${OUTDIR}/perf_summary.csv}"

HIER_DEPTH="${HIER_DEPTH:-0}"          # search -depth n
NUM_CPUS="${NUM_CPUS:-0}"              # DOESN"T EFFECT ANYTHING FOR PERF
PERF_SECONDS="${PERF_SECONDS:-10}"
PERF_RAY_MEM="${PERF_RAY_MEM:-0}"      # 0 = uncapped

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
die() { echo "ERROR: $*" >&2; exit 1; }

need_exec() { [[ -x "$1" ]] || die "Missing executable: $1"; }
need_dir()  { [[ -d "$1" ]] || die "Missing directory: $1"; }

abs_path() {
    local p="$1"
    
    if command -v realpath >/dev/null 2>&1; then
	realpath "$p"
    else
	(cd "$p" && pwd -P)
    fi
}

csv_escape() {
    local s="$1"
    s="${s//\"/\"\"}"
    printf '"%s"' "$s"
}

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

get_objects() {
    local gfile="$1"
    local depth_arg=""

    if [[ -n "${HIER_DEPTH:-}" ]]; then
        depth_arg="-maxdepth $HIER_DEPTH"
    fi

    log DEBUG "MGED search depth_arg='$depth_arg' file='$gfile'"

    {
        printf 'search . %s\n' "$depth_arg"
        printf 'q\n'
    } | "$MGED" -c "$gfile" 2>&1 \
      | tr -d '\r' \
      | awk 'NF && $0 !~ /^mged>/ && $0 !~ /^BRL-CAD/'
}

parse_perf_output() {
    local f="$1"

    local rays wall cpu pool reuse
    rays="$(awk '/^[[:space:]]*Rays\/sec \[wall\][[:space:]]*\(/ {print $NF; exit}' "$f" || true)"
    wall="$(awk '/^[[:space:]]*Wall clock time[[:space:]]*\(/ {print $NF; exit}' "$f" || true)"
    cpu="$(awk '/^[[:space:]]*CPU time[[:space:]]*\(/ {print $NF; exit}' "$f" || true)"
    pool="$(awk '/^[[:space:]]*Ray pool size[[:space:]]*\(/ {print $NF; exit}' "$f" || true)"
    reuse="$(awk '/^[[:space:]]*Pool reuse[[:space:]]*\(/ {print $NF; exit}' "$f" || true)"

    echo "${rays}|${wall}|${cpu}|${pool}|${reuse}"
}

calc_delta() {
    local r1="$1"
    local r2="$2"

    awk -v r1="$r1" -v r2="$r2" '
      function isnum(x) { return x ~ /^([0-9]*\.)?[0-9]+([eE][-+]?[0-9]+)?$/ }
      BEGIN {
        if (!isnum(r1) || !isnum(r2) || r1 == 0 || r2 == 0) {
          print "||"
          exit
        }

        # Throughput metric: higher is better.
        ratio = (r2 >= r1) ? (r2 / r1) : -(r1 / r2)
        pct = ((r2 - r1) / r1) * 100.0

        printf "%.6g|%.3f", ratio, pct
      }
    '
}

run_perf() {
    local cmd="$1"
    local gfile="$2"
    local obj="$3"
    local outfile="$4"

    "$cmd" \
        --perf-seconds "$PERF_SECONDS" \
        --perf-max_memory "$PERF_RAY_MEM" \
        -n "$NUM_CPUS" \
        -p "$gfile" "$obj" >"$outfile" 2>&1
}

main() {
    need_exec "$CMD1"
    need_exec "$CMD2"
    need_exec "$MGED"
    mkdir -p "$OUTDIR"

    echo "file,object,cmd1_rays_per_sec,cmd1_wall_s,cmd1_cpu_s,cmd1_pool_size,cmd1_pool_reuse,cmd2_rays_per_sec,cmd2_wall_s,cmd2_cpu_s,cmd2_pool_size,cmd2_pool_reuse,rays_per_sec_ratio_signed,rays_per_sec_pct" >"$OUTCSV"

    # gather .g files
    mapfile -t gfiles < <(discover_g_files || true)
    [[ "${#gfiles[@]}" -gt 0 ]] || die "No .g files found."

    #local tmpdir
    #tmpdir="$(mktemp -d)"
    #trap 'rm -rf "$tmpdir"' EXIT

    for gfile in "${gfiles[@]}"; do
        log "==> $gfile"

        # gather components for this file
        mapfile -t comps < <(get_objects "$gfile" || true)
        [[ "${#comps[@]}" -gt 0 ]] || { log "    (no tops found)"; continue; }

        for obj in "${comps[@]}"; do
            log DEBUG "$obj"
            [[ -n "$obj" ]] || continue
            log "    $obj"

            local safe out1 out2
            safe="$(basename "$gfile").${obj//\//_}"
            #out1="$tmpdir/${safe}.cmd1.txt"
            #out2="$tmpdir/${safe}.cmd2.txt"
            out1="$OUTDIR/${safe}.cmd1.txt"
            out2="$OUTDIR/${safe}.cmd2.txt"

            run_perf "$CMD1" "$gfile" "$obj" "$out1" || true
            run_perf "$CMD2" "$gfile" "$obj" "$out2" || true

            local p1 p2 r1 w1 c1 pool1 reuse1 r2 w2 c2 pool2 reuse2 delta ratio pct
            p1="$(parse_perf_output "$out1")"
            p2="$(parse_perf_output "$out2")"

            IFS='|' read -r r1 w1 c1 pool1 reuse1 <<<"$p1"
            IFS='|' read -r r2 w2 c2 pool2 reuse2 <<<"$p2"

            delta="$(calc_delta "$r1" "$r2")"
            IFS='|' read -r ratio pct <<<"$delta"

            {
                csv_escape "$gfile"; printf ','
                csv_escape "$obj"; printf ','
                printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
                    "$r1" "$w1" "$c1" "$pool1" "$reuse1" \
                    "$r2" "$w2" "$c2" "$pool2" "$reuse2" \
                    "$ratio" "$pct"
            } >>"$OUTCSV"

        done
    done

    {
        echo ""
        echo "#CONFIG,key,value"
        echo "#CONFIG,CMD1,\"$CMD1\""
        echo "#CONFIG,CMD2,\"$CMD2\""
        echo "#CONFIG,MGED,\"$MGED\""
        echo "#CONFIG,MODEL_DIRS,\"$MODEL_DIRS\""
        echo "#CONFIG,HIER_DEPTH,\"$HIER_DEPTH\""
        echo "#CONFIG,NUM_CPUS,\"$NUM_CPUS\""
        echo "#CONFIG,PERF_SECONDS,\"$PERF_SECONDS\""
        echo "#CONFIG,PERF_RAY_MEM,\"$PERF_RAY_MEM\""
    } >>"$OUTCSV"

    log "Done: $OUTCSV"
}

main "$@"

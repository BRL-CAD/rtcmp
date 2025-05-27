#!/bin/bash

# paths to both rtcmp builds
CMD1=/pathto/rtcmp/brl_main-build/rtcmp.exe
CMD2=/pathto/rtcmp/brl_rel-build/rtcmp.exe

# define file / component pairs to run
declare -a FILE_COMP_PAIRS=(
  # "/pathto/models/rtcmp_test.g cube.bot"
  # "/pathto/models/pinewood.g bot_pinewood"
  # ...
)

# tols to check against
TOLS=(1e-15 1e-12 1e-1)

# run rtcmp for each pair
for pair in "${FILE_COMP_PAIRS[@]}"; do
  # split the pair into file and component to run on
  FULLPATH=$(echo "$pair" | awk '{print $1}')
  FILE=$(basename "$FULLPATH")
  COMP=$(echo "$pair" | awk '{print $2}')
  UNIQ_FILE=$FILE.$COMP

  echo ""	# visual separation
  echo "processing: $FULLPATH $COMP"

  # generate both json files. First run auto-generates rays. Second run uses rays from first run
  # rtcmp -d file comp
  GED_START=$(date +%s.%N)
  $CMD1 --output-json "$UNIQ_FILE.json1" --output-rays "$UNIQ_FILE.rays" -d "$FULLPATH" "$COMP"
  $CMD2 --output-json "$UNIQ_FILE.json2" --input-rays "$UNIQ_FILE.rays" -d "$FULLPATH" "$COMP"
  GEN_END=$(date +%s.%N)
  GEN_TIME=$(awk -v e="$GEN_END" -v s="$GEN_START" 'BEGIN{print e - s}')
  echo "[${0##*/}] JSON generation took $GEN_TIME seconds"

  # run comparison
  # rtcmp -c .json1 .json2
  TOL_START=$(date +%s.%N)
  for tol in "${TOLS[@]}"; do
    $CMD1 --output-nirt "$UNIQ_FILE.t$tol.nirt" -t $tol -c "$UNIQ_FILE.json1" "$UNIQ_FILE.json2"
  done
  TOL_END=$(date +%s.%N)
  TOL_TIME=$(awk -v e="$TOL_END" -v s="$TOL_START" 'BEGIN{print e - s}')
  echo "[${0##*/}] comparison(s) took $TOL_TIME seconds"

done

exit 0

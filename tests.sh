#!/bin/bash

CMD1=/pathto/rtcmp/brl_main-build/rtcmp.exe
CMD2=/pathto/rtcmp/brl_rel-build/rtcmp.exe
TOL=1e-10
# define file / component pairs to run
declare -a FILE_COMP_PAIRS=(
  # "/pathto/models/rtcmp_test.g cube.bot"
  # "/pathto/models/pinewood.g bot_pinewood"
  # ...
)

# run rtcmp for each pair
for pair in "${FILE_COMP_PAIRS[@]}"; do
  # split the pair into file and component to run on
  FULLPATH=$(echo "$pair" | awk '{print $1}')
  FILE=$(basename "$FULLPATH")
  COMP=$(echo "$pair" | awk '{print $2}')

  echo ""	# visual separation
  echo "processing: $FULLPATH"

  # generate both json files. First run auto-generates rays. Second run uses rays from first run
  # rtcmp -d file comp
  $CMD1 --output-json "$FILE.json1" --output-rays "$FILE.rays" -d "$FULLPATH" "$COMP"
  $CMD2 --output-json "$FILE.json2" --input-rays "$FILE.rays" -d "$FULLPATH" "$COMP"

  # run comparison
  # rtcmp -c .json1 .json2
  $CMD1 --output-nirt "$FILE.nirt" -t $TOL -c "$FILE.json1" "$FILE.json2"
done

exit 0
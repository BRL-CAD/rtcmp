Tool for comparing the results produced by different raytracers

There are three modes this tool may be run in:

1.  Performance testing (-p option).  Doesn't do anything beyond timing basic shotline behaviors.

2.  Diff input generation (-d option).  Generates JSON input files that can be compared to identify differences in raytrace runs.

3.  Comparison mode (-c option).  Given two JSON output files, compare them and report on observed differences.

A basic comparison workflow will look something like the following:

Compile a version of rtcmp against each version of BRL-CAD you wish to test.  For this example,
we will assume main and a hlbvh branch build of BRL-CAD.

```sh
git clone https://github.com/BRL-CAD/rtcmp.git
cd rtcmp
mkdir build_main
cd build_main
cmake .. -DBRLCAD_ROOT=/home/user/brlcad_main/build
make -j8
cd ..
mkdir build_hlbvh
cd build_hlbvh
cmake .. -DBRLCAD_ROOT=/home/user/brlcad_hlbvh/build
make -j8
cd ..
```

We now have two rtcmp builds, compiled against the versions of interest.  Generate
one canonical rayset with the baseline build, then replay that exact rayset with
the candidate build:

```sh
build_main/rtcmp -d --minimum-rays 100000 --output-rays canonical.rays \
  --output-json baseline.json ~/test.g geom.bot
build_hlbvh/rtcmp -d --input-rays canonical.rays \
  --output-json candidate.json ~/test.g geom.bot
build_main/rtcmp -c --input-rays canonical.rays \
  --output-status correctness.json baseline.json candidate.json
```

Operation exits are `0` success, `1` execution failure, `2` usage/configuration,
`3` I/O, `4` scene setup, `5` invalid rayset, `6` geometric difference, and `7`
malformed or incomplete result coverage.  The structured status records the
versioned canonical-rayset identity, exact coverage counts, tolerance, and
mismatch classes.  When results differ, `diff.nrt` contains the strict ray
records needed to reproduce those shots.  A no-findings report has state
`equivalent`, exit code `0`, and zero `different_rays`; no stale difference file
is retained.

Use `-d --primitive-hits` with the regular RT engine to include pre-Boolean
primitive segments in each ray record alongside evaluated region partitions.
Segment records include primitive identity, transform, entry and exit
distances, points, outward normals, and surface numbers. Points and normals are
null for malformed segments that cannot be evaluated safely. This mode also
records rays whose primitive hits are removed by Boolean evaluation. `-c`
compares segments automatically when both files contain them and classifies
primitive and evaluated differences separately. Mixing primitive-enabled and
evaluated-only result files is an invalid comparison.

Every generation mode records every canonical ray, including misses. Comparison
therefore rejects missing, duplicate, malformed, and out-of-rayset records
instead of guessing whether an absent record was a miss. Existing unversioned
NDJSON results remain usable only when their parsed ray keys exactly cover the
supplied canonical rayset; lossy legacy ray coordinates are not matched by
tolerance and should be regenerated.

To filter likely grazing differences when the geometry is available, provide it during comparison:

```sh
./rtcmp -c --grazing-geometry model.g --grazing-object geom \
    results1.json results2.json
```

This re-shoots each differing ray whose region hit count changed, plus eight parallel rays on a small circle around it. If the implicated region's hit count varies across the circle, rtcmp counts the difference as likely grazing and omits it from `diff.nrt`. The default circle radius is `1e-7` times the prepared model radius; `--grazing-radius` sets an absolute radius in millimeters and `--grazing-samples` changes the number of rays on the circle. This is a heuristic using the BRL-CAD raytracer linked to the comparison executable. Differences in normals or distances without a change in region hit count remain in the report. Comparisons without the geometry options do not re-shoot rays.

IMPORTANT: the plot file produced is NOT a visualization of the differences, but rather the partitions that are involved with differences.  The difference themselves are often far too small to be visible graphically in a plot, or might be region name or normal based rather than an actual difference in solid thicknesses.  The plot is useful for identifying what parts of a scene are involved in producing the differences - to really understand the root cause, it is typically necessary to step through a nirt shotline in a debugger and determine where the mathematics of the answer is being altered.


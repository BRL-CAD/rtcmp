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

We now have two rtcmp builds, compiled against the versions of interest.  Next, we generate inputs:

```sh
cd build_main && ./rtcmp -d ~/test.g geom.bot
cd ..
cd build_hlbvh && ./rtcmp -d ~/test.g geom.bot
./rtcmp -c ../build_main/shots.json shots.json
```

This will produce a summary of what was observed, as well as a plot file showing segments involved  with differences and a text file with NIRT commands for reproducing differing shotlines.

Use `-d --primitive-hits` to include pre-Boolean primitive segments in each ray record alongside evaluated region partitions. Segment records include primitive identity, transform, entry and exit distances, points, outward normals, and surface numbers. Points and normals are null for malformed segments that cannot be evaluated safely. This mode also records rays whose primitive hits are removed by Boolean evaluation. `-c` automatically compares segments when both input files include them and reports primitive-only, evaluated-only, and combined differences. Comparing a primitive-enabled file with a legacy file is an error. In primitive mode, `--skip-misses` omits only rays with neither segments nor partitions.

Without `--primitive-hits`, use `-d --skip-misses` when generating a result file to omit rays with no partitions from `shots.json`. The option keeps hit records and leaves `shots.rays` intact so both builds can fire the same rays. When comparing results, a recorded miss and an absent ray are treated as equal by default. A recorded hit with no matching ray remains a difference. Use `-c --report-missing-rays` to report every ray present in only one file, including recorded misses.

To filter likely grazing differences when the geometry is available, provide it during comparison:

```sh
./rtcmp -c --grazing-geometry model.g --grazing-object geom \
    results1.json results2.json
```

This re-shoots each differing ray whose region hit count changed, plus eight parallel rays on a small circle around it. If the implicated region's hit count varies across the circle, rtcmp counts the difference as likely grazing and omits it from `diff.nrt`. The default circle radius is `1e-7` times the prepared model radius; `--grazing-radius` sets an absolute radius in millimeters and `--grazing-samples` changes the number of rays on the circle. This is a heuristic using the BRL-CAD raytracer linked to the comparison executable. Differences in normals or distances without a change in region hit count remain in the report. Comparisons without the geometry options do not re-shoot rays.

IMPORTANT: the plot file produced is NOT a visualization of the differences, but rather the partitions that are involved with differences.  The difference themselves are often far too small to be visible graphically in a plot, or might be region name or normal based rather than an actual difference in solid thicknesses.  The plot is useful for identifying what parts of a scene are involved in producing the differences - to really understand the root cause, it is typically necessary to step through a nirt shotline in a debugger and determine where the mathematics of the answer is being altered.


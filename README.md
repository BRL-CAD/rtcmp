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

IMPORTANT: the plot file produced is NOT a visualization of the differences, but rather the partitions that are involved with differences.  The difference themselves are often far too small to be visible graphically in a plot, or might be region name or normal based rather than an actual difference in solid thicknesses.  The plot is useful for identifying what parts of a scene are involved in producing the differences - to really understand the root cause, it is typically necessary to step through a nirt shotline in a debugger and determine where the mathematics of the answer is being altered.


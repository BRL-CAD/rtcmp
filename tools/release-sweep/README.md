# BRL-CAD release sweep

This proof of concept builds one BRL-CAD revision at a time, links the current
rtcmp source against it, and retains a small self-contained runtime bundle.
After all bundles exist, a separate step compares each release with one pinned
main build using the same geometry and canonical rayset.

The split between building and comparing is intentional:

- A failed build remains available for diagnosis.
- A full BRL-CAD build is deleted only after the packaged runtime passes a
  shotline smoke test.
- Comparisons can be repeated without rebuilding BRL-CAD.
- Runtime bundles contain only the transitive DLL closure of `rtcmp.exe`, plus
  the small rtcmp build directory and a hash manifest.

## Scope and limitations

The initial release set is `rel-7-46-0`, `rel-7-44-0`, `rel-7-42-2`, and
`rel-7-40-0`. Always spell release refs as `refs/tags/<tag>` because a local
branch can have the same name as a tag.

Both correctness passes use ordinary evaluated shotlines, so every listed
release supports them. The first pass reports the raw shotline comparison. The
second enables rtcmp's grazing-ray detector to classify likely edge-instability
findings. The detector belongs to the current rtcmp comparator; candidate
releases do not need BRL-CAD's newer primitive-capture instrumentation.

The edge pass is deliberately run by the pinned main rtcmp bundle. It re-shoots
only differing rays against the supplied geometry using main's librt. This
holds the classification model constant while the captured release results
vary. Primitive-segment capture is smoke-tested when a BRL-CAD revision exposes
that optional hook, but it is not the edge comparison or a release-sweep result.

All revisions use one supplied current bext bundle. This controls dependency
variation and avoids rebuilding the full historical dependency stack, but it
means the result is a BRL-CAD source trend rather than a benchmark of the
original binary distributions. Geogram, Manifold, and UtahRLE are compatibility
exceptions for the two releases that require their historical interfaces.

The performance samples are deliberately short and single-threaded. They are
useful for screening trends, not for publishing benchmark claims. Investigate
large changes with longer runs on an otherwise idle host. The run manifest
records timestamps and basic host identity so results from different machines
are not accidentally treated as one trend.

## Prerequisites

Run the scripts from an x64 Visual Studio developer PowerShell. They require
Git, CMake, Ninja, `cl.exe`, and `dumpbin.exe` on `PATH`, plus an existing
Release build of the BRL-CAD external dependency bundle. Supplying that bundle
is mandatory so a release configure cannot unexpectedly download and build
the complete dependency stack.

The source patch in this directory only quotes an empty value in historical
CMake packaging logic. It is required by the current Windows Ninja toolchain
and does not alter BRL-CAD runtime code.

The builder configures `BRLCAD_COMPONENTS=librt`, allowing CMake to expand only
librt's library dependency closure. `BRLCAD_ENABLE_MINIMAL` alone is not a
component selector in the older releases and would still configure unrelated
applications and their dependencies.

### Legacy dependencies

`rel-7-42-2` and `rel-7-40-0` require older Geogram, Manifold, and UtahRLE
interfaces that are absent from, or incompatible with, the current external
dependency bundle. Build each release's dependencies once from its matching
bext history and keep the installations separate. The exact pins are:

- `rel-7-42-2`: bext `f9074f84c87605f89d912069cee1b1e710ead635`,
  Geogram `888172e37ea4729469fa2c5dbbc15ba68b8c234e`, Manifold
  `edb4b25f647e23312d823c8bd6eb662ce9b1c9a8`, UtahRLE
  `af1fe23824e2ea1865bfa4b35be0cbc9d4c7177d`, and Geogram patches
  `geogram_cmake.patch`, `geogram_win32.patch`, and `geogram_freebsd.patch`.
- `rel-7-40-0`: bext `cef79c30221fb8dc8c860fe85247972fdaa08f01`,
  Geogram `888172e37ea4729469fa2c5dbbc15ba68b8c234e`, Manifold
  `edb4b25f647e23312d823c8bd6eb662ce9b1c9a8`, UtahRLE
  `9250e4eb49b90afaa69d30625b7df4f867b411fc`, and Geogram patches
  `geogram_cmake.patch` and `geogram_win32.patch`.

The commands below show the `rel-7-40-0` setup. Create a detached bext
worktree, initialize its pinned submodules, and apply the patches in bext
order:

```powershell
$bextRepository = 'C:\path\to\bext'
$work = 'C:\path\to\release-sweep-work'
$legacyBextSource = Join-Path $work 'bext-source\rel-7-40-0'
$geogramSource = Join-Path $legacyBextSource 'geogram\geogram'
$geogramBuild = Join-Path $work 'bext-build\rel-7-40-0-geogram'
$legacyDependencies = Join-Path $work 'legacy-dependency-prefix\rel-7-40-0'

git -C $bextRepository worktree add --detach $legacyBextSource `
    cef79c30221fb8dc8c860fe85247972fdaa08f01
git -C $legacyBextSource submodule update --init `
    geogram/geogram manifold/manifold utahrle/utahrle
foreach ($patch in @(
        'geogram_cmake.patch',
        'geogram_win32.patch'
    )) {
    git -C $geogramSource apply -p1 `
        (Join-Path $legacyBextSource "geogram\$patch")
}
```

Then configure and install it in the same x64 developer shell:

```powershell
cmake -S $geogramSource -B $geogramBuild -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_INSTALL_PREFIX=$legacyDependencies" `
    -DGEOGRAM_LIB_ONLY=ON `
    -DGEOGRAM_WITH_GRAPHICS=OFF `
    -DGEOGRAM_WITH_LUA=OFF `
    -DGEOGRAM_WITH_HLBFGS=OFF `
    -DGEOGRAM_WITH_TETGEN=OFF `
    -DGEOGRAM_WITH_TRIANGLE=OFF `
    -DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=ON
cmake --build $geogramBuild --target install

$manifoldSource = Join-Path $legacyBextSource 'manifold\manifold'
$manifoldBuild = Join-Path $work 'bext-build\legacy-manifold'
$currentDependencies = 'C:\path\to\current-bext\build\bundled\Release\install'
cmake -S $manifoldSource -B $manifoldBuild -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_INSTALL_PREFIX=$legacyDependencies" `
    -DCMAKE_INSTALL_LIBDIR=lib `
    "-DASSETIMPORT_ROOT=$currentDependencies" `
    -DMANIFOLD_TEST=OFF `
    -DMANIFOLD_EXPORT=ON `
    -DGLM_BUILD_LIBRARY=OFF `
    -DGLM_BUILD_INSTALL=ON `
    -DBUILD_SHARED_LIBS=ON `
    -DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=ON
cmake --build $manifoldBuild --target install

$utahrleSource = Join-Path $legacyBextSource 'utahrle\utahrle'
$utahrleBuild = Join-Path $work 'bext-build\legacy-utahrle'
cmake -S $utahrleSource -B $utahrleBuild -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_INSTALL_PREFIX=$legacyDependencies" `
    -DBUILD_SHARED_LIBS=ON `
    -DBUILD_STATIC_LIBS=OFF
cmake --build $utahrleBuild --target install
```

Repeat the dependency build with the `rel-7-42-2` pins above and a separate
source, build, and installation prefix when building the complete default set.

The builder validates each dependency's installed header, import library, and
DLL. It also records the bext and dependency source commits and the installed
library hashes in each affected runtime bundle. It supplies Manifold's include
root and library explicitly. After configure, it also replaces the build
tree's staged current Manifold and GLM headers with their pinned historical
counterparts and records deterministic hashes of both complete header trees.
This is necessary because the staged include directory precedes the configured
system include path, while historical Manifold exposes GLM types in its public
API. After both legacy release bundles pass their post-cleanup smoke tests,
these dependency sources, builds, and the shared install directory are no
longer needed.

For a legacy build, the runtime dependency walker searches the pinned legacy
prefix before the BRL-CAD build tree. It verifies any retained legacy DLL
against the pinned installation hash, preventing a staged current DLL with the
same filename from being packaged accidentally.

The two historical `rt_i` definitions expose ray-tracing counters directly,
whereas current rtcmp reads the same counters through a nested `stats` member.
After `librt` is built, the builder stages the revision's public headers in the
install-style include layout expected by rtcmp. Component-only builds do not
consistently run BRL-CAD's public-header copy targets. For legacy revisions,
the builder then adds an anonymous-union compatibility view to that staged
header. The counter order and size remain unchanged, and the adapted header
hash is recorded in metadata. The legacy rtcmp compile also defines `NOMINMAX`
to prevent old Windows compatibility headers from rewriting C++
standard-library calls.

Those releases also require callers to allocate per-CPU resources before a
parallel scene prep. Since this proof of concept intentionally measures one
CPU, the same staged header maps rtcmp's prep call to the historical serial
entry point. Scene preparation occurs before the timed ray loop.

## Build retained runtimes

Choose explicit work and output directories. The work directory is disposable;
the runtime directory is retained.

```powershell
$brlcad = 'C:\path\to\brlcad'
$bext = 'C:\path\to\bext\build\bundled\Release'
$work = 'C:\path\to\release-sweep-work'
$runtimes = 'C:\path\to\release-sweep\runtimes'
$geometry = 'C:\path\to\fixture\box.g'
$legacyDependencies7422 = 'C:\path\to\legacy-dependency-prefix\rel-7-42-2'
$legacyDependencies7400 = 'C:\path\to\legacy-dependency-prefix\rel-7-40-0'

$mainCommit = (git -C $brlcad rev-parse 'refs/heads/main^{commit}').Trim()
.\tools\release-sweep\build-runtime.ps1 `
    -BrlcadRepository $brlcad -BextDirectory $bext `
    -Ref $mainCommit -Name main -WorkDirectory $work `
    -OutputDirectory $runtimes -SmokeGeometry $geometry

foreach ($tag in @('rel-7-46-0', 'rel-7-44-0')) {
    .\tools\release-sweep\build-runtime.ps1 `
        -BrlcadRepository $brlcad -BextDirectory $bext `
        -Ref "refs/tags/$tag" -Name $tag -WorkDirectory $work `
        -OutputDirectory $runtimes -SmokeGeometry $geometry
}

$legacyReleases = @(
    [pscustomobject]@{
        Tag = 'rel-7-42-2'
        DependencyRoot = $legacyDependencies7422
        BextCommit = 'f9074f84c87605f89d912069cee1b1e710ead635'
        UtahRleCommit = 'af1fe23824e2ea1865bfa4b35be0cbc9d4c7177d'
    },
    [pscustomobject]@{
        Tag = 'rel-7-40-0'
        DependencyRoot = $legacyDependencies7400
        BextCommit = 'cef79c30221fb8dc8c860fe85247972fdaa08f01'
        UtahRleCommit = '9250e4eb49b90afaa69d30625b7df4f867b411fc'
    }
)
foreach ($release in $legacyReleases) {
    .\tools\release-sweep\build-runtime.ps1 `
        -BrlcadRepository $brlcad -BextDirectory $bext `
        -Ref "refs/tags/$($release.Tag)" -Name $release.Tag -WorkDirectory $work `
        -OutputDirectory $runtimes -SmokeGeometry $geometry `
        -LegacyDependencyRoot $release.DependencyRoot `
        -LegacyBextCommit $release.BextCommit `
        -LegacyGeogramCommit 888172e37ea4729469fa2c5dbbc15ba68b8c234e `
        -LegacyManifoldCommit edb4b25f647e23312d823c8bd6eb662ce9b1c9a8 `
        -LegacyUtahrleCommit $release.UtahRleCommit
}
```

The builder refuses to overwrite a work path or runtime bundle. On failure,
inspect the preserved source and build directories, then rerun the same command
with `-Resume` after correcting a scripted compatibility issue. Resume mode
requires the expected source and BRL-CAD build directories, verifies the pinned
commit and exact recorded patch set, and reuses any completed build work. On
success, the BRL-CAD source worktree and full build are removed, and the
retained bundle is tested again after deletion.

Always use the fresh rtcmp build directory created by the builder. The current
`FindBRLCAD.cmake` caches `BRLCAD_LIBRARIES`, so reconfiguring a pre-existing
build tree created before the link-list reduction can retain its old library
list. The builder's no-overwrite rule avoids that cache ambiguity.

## Run comparisons

Use a new run directory for every sweep. The comparator refuses to overwrite
one, verifies runtime hashes and rtcmp source fingerprints, and interleaves
each release's performance samples with main to reduce drift. Each rtcmp
process is isolated, and the runner retries only Windows' transient
`STATUS_INSUFFICIENT_RESOURCES` result up to three times before failing.

For each release, the runner captures ordinary shotlines once, then invokes the
pinned main comparator twice: first without filtering, and then with
`--grazing-geometry` and `--grazing-object`. The summary records both states,
both remaining-difference counts, and the number of findings filtered by the
edge detector.

Use a small bounded-primitive model for the performance workload. In
particular, avoid half-space geometry: older librt versions print a diagnostic
while computing many half-space normals, so console I/O would dominate the
timing. The comparator rejects that known diagnostic instead of reporting a
misleading regression. A single RPP region is sufficient for this smoke-level
trend check. `-PerformanceGeometry` and `-PerformanceObject` may select this
bounded workload independently of the more varied correctness geometry; when
omitted, they default to `-Geometry` and `-Object`.

```powershell
.\tools\release-sweep\compare-runtimes.ps1 `
    -RuntimeDirectory $runtimes `
    -RunDirectory 'C:\path\to\release-sweep\runs\first-poc' `
    -Geometry $geometry `
    -PerformanceGeometry $performanceGeometry `
    -PerformanceObject 'all.g' `
    -MinimumRays 6 `
    -PerformanceSeconds 1 `
    -PerformanceRepetitions 3
```

The run retains the canonical rayset and baseline shotline JSON so later
bundles can be compared with the same baseline, and its manifest records hashes
for both the rayset and geometry. Candidate JSON is removed after both valid
comparisons. Equivalent runs keep only compact shotline, edge, and performance
JSON. Differing runs additionally keep each pass's strict `.nrt` rayset.

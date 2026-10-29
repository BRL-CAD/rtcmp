[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BrlcadRepository,

    [Parameter(Mandatory = $true)]
    [string]$BextDirectory,

    [Parameter(Mandatory = $true)]
    [string]$Ref,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$Name,

    [Parameter(Mandatory = $true)]
    [string]$WorkDirectory,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $true)]
    [string]$SmokeGeometry,

    [string]$SmokeObject = 'all.g',

    [string]$LegacyDependencyRoot,

    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$LegacyBextCommit,

    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$LegacyGeogramCommit,

    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$LegacyManifoldCommit,

    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$LegacyUtahrleCommit,

    [switch]$Resume
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$rtcmpSource = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$brlcadRepository = [IO.Path]::GetFullPath($BrlcadRepository)
$bextDirectory = [IO.Path]::GetFullPath($BextDirectory)
$workDirectory = [IO.Path]::GetFullPath($WorkDirectory)
$outputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$smokeGeometry = [IO.Path]::GetFullPath($SmokeGeometry)
$legacyDependencyRoot = if ($LegacyDependencyRoot) {
    [IO.Path]::GetFullPath($LegacyDependencyRoot)
} else {
    $null
}
$sourceDirectory = Join-Path $workDirectory "source\$Name"
$brlcadBuild = Join-Path $workDirectory "brlcad-build\$Name"
$rtcmpBuild = Join-Path $workDirectory "rtcmp-build\$Name"
$smokeDirectory = Join-Path $workDirectory "smoke\$Name"
$bundleStaging = Join-Path $workDirectory "bundle\$Name"
$bundleDirectory = Join-Path $outputDirectory $Name
$compatibilityPatch = Join-Path $PSScriptRoot 'cmake-cpack-empty-processor.patch'

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [switch]$Quiet
    )

    $output = $null
    if ($Quiet) {
        $savedErrorActionPreference = $ErrorActionPreference
        try {
            # Historical librt writes benign diagnostics to stderr. Capture
            # them without letting PowerShell turn native stderr into errors.
            $ErrorActionPreference = 'Continue'
            $output = (& $Command @Arguments 2>&1 | Out-String)
            $exitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $savedErrorActionPreference
        }
    } else {
        & $Command @Arguments
        $exitCode = $LASTEXITCODE
    }
    if ($exitCode -ne 0) {
        $detail = if ($output) { ":`n$output" } else { '' }
        throw "$Command exited with status $exitCode$detail"
    }
}

function Assert-ChildPath {
    param(
        [Parameter(Mandatory = $true)][string]$Parent,
        [Parameter(Mandatory = $true)][string]$Child
    )

    $parentPath = [IO.Path]::GetFullPath($Parent).TrimEnd('\')
    $childPath = [IO.Path]::GetFullPath($Child)
    if (-not $childPath.StartsWith(
            $parentPath + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path '$childPath' is outside '$parentPath'"
    }
}

function Get-DirectoryFingerprint {
    param(
        [Parameter(Mandatory = $true)][string]$Directory
    )

    $root = [IO.Path]::GetFullPath($Directory).TrimEnd('\')
    $entries = Get-ChildItem -LiteralPath $root -Recurse -File |
        Sort-Object FullName |
        ForEach-Object {
            $relativePath = $_.FullName.Substring($root.Length + 1).Replace('\', '/')
            "$relativePath $((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash)"
        }
    if (-not $entries) {
        throw "Cannot fingerprint empty directory: $root"
    }

    $hash = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes(($entries -join "`n"))
        return [BitConverter]::ToString($hash.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant()
    } finally {
        $hash.Dispose()
    }
}

function Set-LegacyManifoldHeaders {
    param(
        [Parameter(Mandatory = $true)][string]$DependencyRoot,
        [Parameter(Mandatory = $true)][string]$BuildDirectory
    )

    $fingerprints = [ordered]@{}
    foreach ($headerDirectory in @('manifold', 'glm')) {
        $source = Join-Path $DependencyRoot "include\$headerDirectory"
        $destination = Join-Path $BuildDirectory "include\$headerDirectory"
        if (-not (Test-Path -LiteralPath $source -PathType Container)) {
            throw "Legacy Manifold support header directory does not exist: $source"
        }
        Assert-ChildPath $BuildDirectory $destination
        if (Test-Path -LiteralPath $destination) {
            Remove-Item -LiteralPath $destination -Recurse -Force
        }
        Copy-Item -LiteralPath $source -Destination $destination -Recurse

        $sourceFingerprint = Get-DirectoryFingerprint $source
        $destinationFingerprint = Get-DirectoryFingerprint $destination
        if ($destinationFingerprint -ne $sourceFingerprint) {
            throw "Staged headers do not match '$source'"
        }
        $fingerprints[$headerDirectory] = $destinationFingerprint
    }
    return $fingerprints
}

function Initialize-BrlcadClientHeaders {
    param(
        [Parameter(Mandatory = $true)][string]$SourceDirectory,
        [Parameter(Mandatory = $true)][string]$BuildDirectory
    )

    $sourceInclude = Join-Path $SourceDirectory 'include'
    $stagedInclude = Join-Path $BuildDirectory 'include\brlcad'
    Assert-ChildPath $BuildDirectory $stagedInclude
    New-Item -ItemType Directory -Path $stagedInclude -Force | Out-Null

    # Component-only builds do not consistently run BRL-CAD's public-header
    # copy targets. Recreate the install layout from the pinned source so
    # rtcmp never needs the disposable worktree on its include path.
    foreach ($sourceHeader in Get-ChildItem -LiteralPath $sourceInclude -Recurse -File -Filter '*.h') {
        $relativePath = $sourceHeader.FullName.Substring($sourceInclude.Length + 1)
        $destination = Join-Path $stagedInclude $relativePath
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $sourceHeader.FullName -Destination $destination -Force
    }

    $header = Join-Path $stagedInclude 'rt\rt_instance.h'
    Assert-ChildPath $BuildDirectory $header
    if (-not (Test-Path -LiteralPath $header -PathType Leaf)) {
        throw "Staged BRL-CAD runtime header does not exist: $header"
    }
    return $header
}

function Enable-LegacyRtcmpHeaderCompatibility {
    param(
        [Parameter(Mandatory = $true)][string]$BuildDirectory,
        [Parameter(Mandatory = $true)][string]$Header
    )

    $header = [IO.Path]::GetFullPath($Header)
    Assert-ChildPath $BuildDirectory $header

    $content = [IO.File]::ReadAllText($header)
    $statisticsMarker = 'rtcmp legacy statistics compatibility view'
    $prepMarker = 'rtcmp legacy serial-prep compatibility'
    if ($content.Contains($statisticsMarker) -and $content.Contains($prepMarker)) {
        return (Get-FileHash -LiteralPath $header -Algorithm SHA256).Hash.ToLowerInvariant()
    }

    $newline = if ($content.Contains("`r`n")) { "`r`n" } else { "`n" }
    $statisticsFields = @(
        '    size_t              nregions;       /**< @brief  total # of regions participating */',
        '    size_t              nsolids;        /**< @brief  total # of solids participating */',
        '    size_t              rti_nrays;      /**< @brief  # calls to rt_shootray() */',
        '    size_t              nmiss_model;    /**< @brief  rays missed model RPP */',
        '    size_t              nshots;         /**< @brief  # of calls to ft_shot() */',
        '    size_t              nmiss;          /**< @brief  solid ft_shot() returned a miss */',
        '    size_t              nhits;          /**< @brief  solid ft_shot() returned a hit */',
        '    size_t              nmiss_tree;     /**< @brief  shots missed sub-tree RPP */',
        '    size_t              nmiss_solid;    /**< @brief  shots missed solid RPP */',
        '    size_t              ndup;           /**< @brief  duplicate shots at a given solid */',
        '    size_t              nempty_cells;   /**< @brief  number of empty spatial partition cells passed through */'
    )
    if (-not $content.Contains($statisticsMarker)) {
        $original = (@('    /* Ray-tracing statistics */') + $statisticsFields) -join $newline
        if (-not $content.Contains($original)) {
            throw "Historical rt_i statistics layout is not recognized in '$header'"
        }

        $nestedFields = @($statisticsFields | ForEach-Object { "        $_" })
        $replacement = @(
            '    /* Ray-tracing statistics */',
            "    /* $statisticsMarker; layout is unchanged. */",
            '    union {',
            '        struct {'
        ) + $nestedFields + @(
            '        };',
            '        struct {'
        ) + $nestedFields + @(
            '        } stats;',
            '    };'
        )
        $content = $content.Replace($original, ($replacement -join $newline))
    }

    if (-not $content.Contains($prepMarker)) {
        $includeGuardEnd = '#endif /* RT_RT_INSTANCE_H */'
        if (-not $content.Contains($includeGuardEnd)) {
            throw "Historical rt_i include guard is not recognized in '$header'"
        }
        $serialPrep = @(
            "/* $prepMarker; benchmarks use one CPU. */",
            '#define rt_prep_parallel(_rtip, _ncpu) rt_prep((_rtip))',
            '',
            $includeGuardEnd
        ) -join $newline
        $content = $content.Replace($includeGuardEnd, $serialPrep)
    }

    [IO.File]::WriteAllText($header, $content, [Text.UTF8Encoding]::new($false))
    return (Get-FileHash -LiteralPath $header -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Ensure-GitPatch {
    param(
        [Parameter(Mandatory = $true)][string]$Repository,
        [Parameter(Mandatory = $true)][string]$Patch
    )

    $savedErrorActionPreference = $ErrorActionPreference
    try {
        # A failed applicability probe is expected for an already-applied
        # patch, so do not promote git's diagnostic stderr to a terminating
        # PowerShell error.
        $ErrorActionPreference = 'Continue'
        & git.exe -C $Repository apply --check $Patch 2>$null
        $canApply = $LASTEXITCODE -eq 0
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if ($canApply) {
        Invoke-Checked git.exe @('-C', $Repository, 'apply', $Patch)
        return
    }

    try {
        $ErrorActionPreference = 'Continue'
        & git.exe -C $Repository apply --reverse --check $Patch 2>$null
        $isApplied = $LASTEXITCODE -eq 0
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if (-not $isApplied) {
        throw "Patch is neither applicable nor already applied: $Patch"
    }
}

function Get-SourceFingerprint {
    $sourcePaths = @(
        (Join-Path $rtcmpSource 'CMake'),
        (Join-Path $rtcmpSource 'comp'),
        (Join-Path $rtcmpSource 'dry'),
        (Join-Path $rtcmpSource 'perf'),
        (Join-Path $rtcmpSource 'rt'),
        (Join-Path $rtcmpSource 'tie'),
        (Join-Path $rtcmpSource 'util')
    )
    $files = @(Get-ChildItem -LiteralPath $sourcePaths -Recurse -File)
    $files += @(
        (Get-Item -LiteralPath (Join-Path $rtcmpSource 'CMakeLists.txt')),
        (Get-Item -LiteralPath (Join-Path $rtcmpSource 'cxxopts.hpp')),
        (Get-Item -LiteralPath (Join-Path $rtcmpSource 'rtcmp.cpp')),
        (Get-Item -LiteralPath (Join-Path $rtcmpSource 'rtcmp.h')),
        (Get-Item -LiteralPath (Join-Path $rtcmpSource 'Shot.h')),
        (Get-Item -LiteralPath (Join-Path $rtcmpSource 'shot_sink.h'))
    )
    $entries = foreach ($file in ($files | Sort-Object FullName -Unique)) {
        $relativePath = $file.FullName.Substring($rtcmpSource.Length + 1)
        "$relativePath $((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash)"
    }

    $hash = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes(($entries -join "`n"))
        return [BitConverter]::ToString($hash.ComputeHash($bytes)).Replace('-', '').ToLowerInvariant()
    } finally {
        $hash.Dispose()
    }
}

function Copy-RuntimeClosure {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$SearchDirectories,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $dumpbin = (Get-Command dumpbin.exe -ErrorAction Stop).Source
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Copy-Item -LiteralPath $Executable -Destination $Destination

    $queue = [Collections.Generic.Queue[string]]::new()
    $copied = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $queue.Enqueue((Join-Path $Destination 'rtcmp.exe'))
    while ($queue.Count -gt 0) {
        $binary = $queue.Dequeue()
        $dependencyOutput = & $dumpbin /nologo /dependents $binary 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw "Unable to inspect runtime dependencies for '$binary'"
        }
        foreach ($line in $dependencyOutput) {
            if ($line -notmatch '^\s+([^\s]+\.dll)\s*$') {
                continue
            }
            $dependency = $Matches[1]
            $source = $SearchDirectories |
                ForEach-Object { Join-Path $_ $dependency } |
                Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
                Select-Object -First 1
            if ($source -and $copied.Add($dependency)) {
                Copy-Item -LiteralPath $source -Destination $Destination
                $queue.Enqueue((Join-Path $Destination $dependency))
            }
        }
    }
}

function Invoke-BundleSmoke {
    param(
        [Parameter(Mandatory = $true)][string]$Bundle,
        [Parameter(Mandatory = $true)][bool]$PrimitiveCapture
    )

    if (Test-Path -LiteralPath $smokeDirectory) {
        Remove-Item -LiteralPath $smokeDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Path $smokeDirectory -Force | Out-Null

    $savedPath = $env:PATH
    try {
        $env:PATH = "$Bundle;$env:SystemRoot\System32;$env:SystemRoot"
        $rtcmp = Join-Path $Bundle 'rtcmp.exe'
        Invoke-Checked -Command $rtcmp -Arguments @(
            '-d', '--minimum-rays', '6',
            '--output-rays', (Join-Path $smokeDirectory 'canonical.rays'),
            '--output-json', (Join-Path $smokeDirectory 'standard.json'),
            '--output-status', (Join-Path $smokeDirectory 'standard.status.json'),
            $smokeGeometry, $SmokeObject
        ) -Quiet
        if ($PrimitiveCapture) {
            Invoke-Checked -Command $rtcmp -Arguments @(
                '-d', '--primitive-hits',
                '--input-rays', (Join-Path $smokeDirectory 'canonical.rays'),
                '--output-json', (Join-Path $smokeDirectory 'primitive.json'),
                '--output-status', (Join-Path $smokeDirectory 'primitive.status.json'),
                $smokeGeometry, $SmokeObject
            ) -Quiet
        }
    } finally {
        $env:PATH = $savedPath
    }
}

foreach ($command in @('cmake.exe', 'cl.exe', 'dumpbin.exe', 'git.exe', 'ninja.exe')) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "Required command '$command' is unavailable. Run from an x64 Visual Studio developer shell."
    }
}
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64' -or -not $env:LIB) {
    throw 'The x64 Visual Studio compiler environment is incomplete. Run from an x64 Visual Studio developer shell.'
}
$windowsSdkAvailable = @($env:LIB -split ';' | Where-Object {
        $_ -and (Test-Path -LiteralPath (Join-Path $_ 'kernel32.lib') -PathType Leaf)
    }).Count -gt 0
if (-not $windowsSdkAvailable) {
    throw 'The Windows SDK libraries are unavailable. Run from an x64 Visual Studio developer shell.'
}
foreach ($path in @($brlcadRepository, $bextDirectory, $rtcmpSource)) {
    if (-not (Test-Path -LiteralPath $path -PathType Container)) {
        throw "Required directory does not exist: $path"
    }
}
foreach ($path in @($smokeGeometry, $compatibilityPatch)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file does not exist: $path"
    }
}
$legacyDependencyArgumentsPresent = @(@(
        $legacyDependencyRoot,
        $LegacyBextCommit,
        $LegacyGeogramCommit,
        $LegacyManifoldCommit,
        $LegacyUtahrleCommit
    ) | Where-Object { $_ })
if ($legacyDependencyArgumentsPresent.Count -notin @(0, 5)) {
    throw 'All legacy dependency arguments must be supplied together.'
}
$legacyDependencySpecs = @()
if ($legacyDependencyRoot) {
    $legacyGlmHeader = Join-Path $legacyDependencyRoot 'include\glm\glm.hpp'
    $legacyDependencySpecs = @(
        [ordered]@{
            name = 'geogram'
            source_commit = $LegacyGeogramCommit
            header = Join-Path $legacyDependencyRoot 'include\geogram1\geogram\basic\common.h'
            import_library = Join-Path $legacyDependencyRoot 'lib\geogram.lib'
            runtime_library = Join-Path $legacyDependencyRoot 'bin\geogram.dll'
        },
        [ordered]@{
            name = 'manifold'
            source_commit = $LegacyManifoldCommit
            header = Join-Path $legacyDependencyRoot 'include\manifold\manifold.h'
            import_library = Join-Path $legacyDependencyRoot 'lib\manifold.lib'
            runtime_library = Join-Path $legacyDependencyRoot 'bin\manifold.dll'
        },
        [ordered]@{
            name = 'utahrle'
            source_commit = $LegacyUtahrleCommit
            header = Join-Path $legacyDependencyRoot 'include\utahrle\rle.h'
            import_library = Join-Path $legacyDependencyRoot 'lib\utahrle.lib'
            runtime_library = Join-Path $legacyDependencyRoot 'bin\utahrle.dll'
        }
    )
    foreach ($dependency in $legacyDependencySpecs) {
        foreach ($path in @(
                $dependency.header,
                $dependency.import_library,
                $dependency.runtime_library
            )) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "Legacy $($dependency.name) installation is incomplete: $path"
            }
        }
    }
    if (-not (Test-Path -LiteralPath $legacyGlmHeader -PathType Leaf)) {
        throw "Legacy Manifold GLM installation is incomplete: $legacyGlmHeader"
    }
}
foreach ($path in @($sourceDirectory, $brlcadBuild, $rtcmpBuild, $smokeDirectory, $bundleStaging)) {
    Assert-ChildPath $workDirectory $path
    if (-not $Resume -and (Test-Path -LiteralPath $path)) {
        throw "Refusing to overwrite existing work path: $path"
    }
}
if ($Resume) {
    foreach ($path in @($sourceDirectory, $brlcadBuild)) {
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            throw "Cannot resume because a required work directory is missing: $path"
        }
    }
    foreach ($path in @($rtcmpBuild, $smokeDirectory, $bundleStaging)) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }
}
Assert-ChildPath $outputDirectory $bundleDirectory
if (Test-Path -LiteralPath $bundleDirectory) {
    throw "Refusing to overwrite existing runtime bundle: $bundleDirectory"
}

$commit = & git -C $brlcadRepository rev-parse --verify "${Ref}^{commit}" |
    Select-Object -First 1
if ($LASTEXITCODE -ne 0 -or -not $commit) {
    throw "Unable to resolve BRL-CAD ref '$Ref'"
}
$commit = $commit.Trim()
$primitiveMatch = & git.exe -C $brlcadRepository grep -l `
    'RT_RTCMP_CAPTURE_RUNTIME_TOGGLE' $commit -- include/rt/shoot.h 2>$null
$primitiveProbeExitCode = $LASTEXITCODE
if ($primitiveProbeExitCode -notin @(0, 1)) {
    throw "Unable to inspect primitive-capture support at BRL-CAD commit '$commit'"
}
$primitiveCapture = $primitiveProbeExitCode -eq 0 -and [bool]$primitiveMatch

New-Item -ItemType Directory -Path $workDirectory, $outputDirectory -Force | Out-Null
if ($Resume) {
    $worktreeCommit = (& git.exe -C $sourceDirectory rev-parse 'HEAD^{commit}').Trim()
    if ($LASTEXITCODE -ne 0 -or $worktreeCommit -ne $commit) {
        throw "Resume worktree is not at expected BRL-CAD commit '$commit'"
    }
} else {
    Invoke-Checked git.exe @('-C', $brlcadRepository, 'worktree', 'add', '--detach', $sourceDirectory, $commit)
}
Ensure-GitPatch -Repository $sourceDirectory -Patch $compatibilityPatch
$expectedSourceChanges = @('misc/CMake/BRLCAD_CPack.cmake')
$actualSourceChanges = @(& git.exe -C $sourceDirectory diff --name-only HEAD)
if ($LASTEXITCODE -ne 0) {
    throw "Unable to inspect tracked changes in '$sourceDirectory'"
}
$actualSourceChanges += @(& git.exe -C $sourceDirectory ls-files --others --exclude-standard)
if ($LASTEXITCODE -ne 0) {
    throw "Unable to inspect untracked files in '$sourceDirectory'"
}
if (Compare-Object `
        -ReferenceObject @($expectedSourceChanges | Sort-Object) `
        -DifferenceObject @($actualSourceChanges | Sort-Object)) {
    throw 'BRL-CAD worktree contains changes other than the recorded compatibility patches'
}

$configureArguments = @(
    '-S', $sourceDirectory,
    '-B', $brlcadBuild,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DBRLCAD_EXT_DIR=$bextDirectory",
    '-DBRLCAD_COMPONENTS=librt',
    '-DBRLCAD_ENABLE_MINIMAL=ON',
    '-DBRLCAD_ENABLE_TCL=OFF',
    '-DBRLCAD_ENABLE_STEP=OFF',
    '-DBRLCAD_ENABLE_GDAL=OFF',
    '-DBRLCAD_ENABLE_ASSETIMPORT=OFF',
    '-DBRLCAD_ENABLE_BULLET=OFF',
    '-DBRLCAD_ENABLE_GCT=OFF',
    '-DBRLCAD_ENABLE_RUNTIME_DEBUG=ON',
    '-DBRLCAD_ENABLE_STRICT=OFF',
    '-DBRLCAD_BUNDLED_LIBS=BUNDLED',
    # Older releases have unconditional test-property calls during configure.
    '-DBUILD_TESTING=ON'
)
if ($legacyDependencyRoot) {
    # Historical BRL-CAD resets package *_ROOT values to its staged bext tree.
    # Its 7.40 and 7.42 find modules preserve these independent cache inputs.
    $configureArguments += @(
        "-DGeogram_INSTALL_PREFIX=$legacyDependencyRoot",
        "-DGEOGRAM_INSTALL_PREFIX=$legacyDependencyRoot",
        "-DMANIFOLD_INCLUDE_DIR=$(Join-Path $legacyDependencyRoot 'include')",
        "-DMANIFOLD_LIBRARY=$(Join-Path $legacyDependencyRoot 'lib\manifold.lib')",
        "-DUTAHRLE_INCLUDE_DIR=$(Join-Path $legacyDependencyRoot 'include\utahrle')",
        "-DUTAHRLE_LIBRARY=$(Join-Path $legacyDependencyRoot 'lib\utahrle.lib')"
    )
}
Invoke-Checked cmake.exe $configureArguments
$legacyManifoldHeaderFingerprints = $null
if ($legacyDependencyRoot) {
    # The build tree stages the current bext headers ahead of system include
    # paths. Replace that staged API with the pinned historical header set.
    $legacyManifoldHeaderFingerprints = Set-LegacyManifoldHeaders `
        -DependencyRoot $legacyDependencyRoot `
        -BuildDirectory $brlcadBuild
}
Invoke-Checked cmake.exe @('--build', $brlcadBuild, '--target', 'librt')
$brlcadClientHeader = Initialize-BrlcadClientHeaders `
    -SourceDirectory $sourceDirectory `
    -BuildDirectory $brlcadBuild
$legacyRtcmpHeaderFingerprint = $null
if ($legacyDependencyRoot) {
    # This adds a nested alias for the historical flat counters without
    # changing their order, size, or the already-built librt ABI.
    $legacyRtcmpHeaderFingerprint = Enable-LegacyRtcmpHeaderCompatibility `
        -BuildDirectory $brlcadBuild `
        -Header $brlcadClientHeader
}

$rtcmpConfigureArguments = @(
    '-S', $rtcmpSource,
    '-B', $rtcmpBuild,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DBRLCAD_ROOT=$brlcadBuild",
    '-DBUILD_TESTING=OFF'
)
if ($legacyDependencyRoot) {
    # Reset a resumed cache first, then append NOMINMAX to CMake's native
    # MSVC flags instead of replacing flags such as /EHsc.
    Invoke-Checked cmake.exe ($rtcmpConfigureArguments + @('-U', 'CMAKE_CXX_FLAGS'))
    $cxxFlagsEntry = Select-String `
        -LiteralPath (Join-Path $rtcmpBuild 'CMakeCache.txt') `
        -Pattern '^CMAKE_CXX_FLAGS:STRING=(.*)$'
    if (-not $cxxFlagsEntry -or $cxxFlagsEntry.Matches.Count -ne 1) {
        throw 'Unable to read CMAKE_CXX_FLAGS from the rtcmp cache'
    }
    $cxxFlags = $cxxFlagsEntry.Matches[0].Groups[1].Value
    Invoke-Checked cmake.exe `
        ($rtcmpConfigureArguments + "-DCMAKE_CXX_FLAGS=$cxxFlags /DNOMINMAX")
} else {
    Invoke-Checked cmake.exe $rtcmpConfigureArguments
}
Invoke-Checked cmake.exe @('--build', $rtcmpBuild, '--target', 'rtcmp')

$runtimeSearchDirectories = @()
if ($legacyDependencyRoot) {
    $runtimeSearchDirectories += Join-Path $legacyDependencyRoot 'bin'
}
$runtimeSearchDirectories += Join-Path $brlcadBuild 'bin'
Copy-RuntimeClosure `
    -Executable (Join-Path $rtcmpBuild 'rtcmp.exe') `
    -SearchDirectories $runtimeSearchDirectories `
    -Destination $bundleStaging
if ($legacyDependencyRoot) {
    foreach ($dependency in $legacyDependencySpecs) {
        $bundledRuntime = Join-Path $bundleStaging (Split-Path -Leaf $dependency.runtime_library)
        if (-not (Test-Path -LiteralPath $bundledRuntime -PathType Leaf)) {
            continue
        }
        $bundledHash = (Get-FileHash -LiteralPath $bundledRuntime -Algorithm SHA256).Hash
        $expectedHash = (Get-FileHash `
                -LiteralPath $dependency.runtime_library `
                -Algorithm SHA256).Hash
        if ($bundledHash -ne $expectedHash) {
            throw "Bundled $($dependency.name) runtime does not match the pinned installation"
        }
    }
}
Copy-Item -LiteralPath $rtcmpBuild -Destination (Join-Path $bundleStaging 'rtcmp-build') -Recurse

Invoke-BundleSmoke -Bundle $bundleStaging -PrimitiveCapture $primitiveCapture

$runtimeFiles = Get-ChildItem -LiteralPath $bundleStaging -File | Sort-Object Name | ForEach-Object {
    [ordered]@{
        name = $_.Name
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$legacyDependencies = @()
if ($legacyDependencyRoot) {
    $legacyDependencies = @($legacyDependencySpecs | ForEach-Object {
        [ordered]@{
            name = $_.name
            bext_commit = $LegacyBextCommit.ToLowerInvariant()
            source_commit = $_.source_commit.ToLowerInvariant()
            import_library_sha256 = (Get-FileHash `
                    -LiteralPath $_.import_library `
                    -Algorithm SHA256).Hash.ToLowerInvariant()
            runtime_library_sha256 = (Get-FileHash `
                    -LiteralPath $_.runtime_library `
                    -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })
}
$metadata = [ordered]@{
    schema = 1
    brlcad_ref = $Ref
    brlcad_commit = $commit
    brlcad_source_patch = (Split-Path -Leaf $compatibilityPatch)
    brlcad_source_patch_sha256 = (Get-FileHash `
            -LiteralPath $compatibilityPatch -Algorithm SHA256).Hash.ToLowerInvariant()
    build_type = 'Release'
    runtime_debug = $true
    rtcmp_commit = (& git -C $rtcmpSource rev-parse HEAD).Trim()
    rtcmp_source_fingerprint = "sha256:$(Get-SourceFingerprint)"
    primitive_capture = $primitiveCapture
    legacy_dependencies = $legacyDependencies
    legacy_manifold_headers_sha256 = $legacyManifoldHeaderFingerprints
    legacy_rtcmp_header_sha256 = $legacyRtcmpHeaderFingerprint
    runtime_files = $runtimeFiles
}
$metadata | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $bundleStaging 'metadata.json') -Encoding UTF8

# Cleanup is deliberately last: a failed build or smoke run remains available to diagnose.
Invoke-Checked git.exe @('-C', $brlcadRepository, 'worktree', 'remove', '--force', $sourceDirectory)
foreach ($path in @($brlcadBuild, $rtcmpBuild)) {
    Assert-ChildPath $workDirectory $path
    Remove-Item -LiteralPath $path -Recurse -Force
}

# This second run proves the retained bundle does not depend on a deleted build tree.
Invoke-BundleSmoke -Bundle $bundleStaging -PrimitiveCapture $primitiveCapture
Remove-Item -LiteralPath $smokeDirectory -Recurse -Force
Move-Item -LiteralPath $bundleStaging -Destination $bundleDirectory

Write-Output "Retained runtime bundle: $bundleDirectory"

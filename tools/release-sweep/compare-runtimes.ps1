[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RuntimeDirectory,

    [Parameter(Mandatory = $true)]
    [string]$RunDirectory,

    [Parameter(Mandatory = $true)]
    [string]$Geometry,

    [string]$Object = 'all.g',

    [string]$PerformanceGeometry,

    [string]$PerformanceObject,

    [ValidatePattern('^rel-[0-9]+-[0-9]+-[0-9]+$')]
    [string[]]$Releases = @(
        'rel-7-46-0',
        'rel-7-44-0',
        'rel-7-42-2',
        'rel-7-40-0'
    ),

    [ValidateRange(1, [int]::MaxValue)]
    [int]$MinimumRays = 6,

    [ValidateRange(1, 100)]
    [int]$PerformanceRepetitions = 3,

    [ValidateRange(0.01, 3600.0)]
    [double]$PerformanceSeconds = 1.0,

    [ValidateRange(1, [long]::MaxValue)]
    [long]$PerformanceMaxMemoryBytes = 16MB,

    [ValidateRange(0.0, [double]::MaxValue)]
    [double]$ComparisonTolerance = 1e-9
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$runtimeDirectory = [IO.Path]::GetFullPath($RuntimeDirectory)
$runDirectory = [IO.Path]::GetFullPath($RunDirectory)
$geometry = [IO.Path]::GetFullPath($Geometry)
$performanceGeometry = if ($PerformanceGeometry) {
    [IO.Path]::GetFullPath($PerformanceGeometry)
} else {
    $geometry
}
$performanceObject = if ($PerformanceObject) { $PerformanceObject } else { $Object }
$baselineBundle = Join-Path $runtimeDirectory 'main'
$baselineDirectory = Join-Path $runDirectory 'baseline'
$resultsDirectory = Join-Path $runDirectory 'results'
$canonicalRays = Join-Path $runDirectory 'canonical.rays'
$runStartedUtc = [DateTimeOffset]::UtcNow.ToString('o')
$performanceSecondsArgument = $PerformanceSeconds.ToString(
    'R', [Globalization.CultureInfo]::InvariantCulture)
$comparisonToleranceArgument = $ComparisonTolerance.ToString(
    'R', [Globalization.CultureInfo]::InvariantCulture)
$floatingPointPattern = '[+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[Ee][+-]?[0-9]+)?'
$insufficientResourcesExitCode = -1073741670 # 0xC000009A
$processAttemptLimit = 3
$processRetryDelaySeconds = 1

function Invoke-RtcmpProcess {
    param(
        [Parameter(Mandatory = $true)][string]$Bundle,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $savedPath = $env:PATH
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        $env:PATH = "$Bundle;$env:SystemRoot\System32;$env:SystemRoot"
        # Some historical primitives write benign diagnostics to stderr.
        $ErrorActionPreference = 'Continue'
        for ($attempt = 1; $attempt -le $processAttemptLimit; ++$attempt) {
            $output = (& (Join-Path $Bundle 'rtcmp.exe') @Arguments 2>&1 | Out-String)
            $exitCode = $LASTEXITCODE
            if ($exitCode -ne $insufficientResourcesExitCode `
                -or $attempt -eq $processAttemptLimit) {
                break
            }

            Write-Warning "rtcmp in '$Bundle' encountered transient Windows resource pressure; retrying ($attempt/$processAttemptLimit)"
            Start-Sleep -Seconds $processRetryDelaySeconds
        }
    } finally {
        $env:PATH = $savedPath
        $ErrorActionPreference = $savedErrorActionPreference
    }

    return [pscustomobject]@{
        exit_code = $exitCode
        output = $output
    }
}

function Invoke-Rtcmp {
    param(
        [Parameter(Mandatory = $true)][string]$Bundle,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][int[]]$AcceptedExitCodes
    )

    $result = Invoke-RtcmpProcess -Bundle $Bundle -Arguments $Arguments
    if ($result.exit_code -notin $AcceptedExitCodes) {
        throw "rtcmp in '$Bundle' exited with unexpected status $($result.exit_code): $($result.output)"
    }
    return $result.exit_code
}

function Invoke-PerformanceSample {
    param([Parameter(Mandatory = $true)][string]$Bundle)

    $result = Invoke-RtcmpProcess -Bundle $Bundle -Arguments @(
        '--perf-seconds', $performanceSecondsArgument,
        '--perf-max_memory', $PerformanceMaxMemoryBytes,
        '-n', '1', '-p', $performanceGeometry, $performanceObject
    )
    if ($result.exit_code -ne 0) {
        throw "Performance run in '$Bundle' failed with status $($result.exit_code): $($result.output)"
    }
    $output = $result.output
    if ($output.Contains('rt_hlf_norm:')) {
        throw "Performance geometry triggered per-ray half-space diagnostics in '$Bundle'; use a bounded primitive fixture"
    }
    $match = [regex]::Match(
        $output,
        "Rays/sec \[wall\] \(rt\):\s+($floatingPointPattern)(?:\s|$)"
    )
    if (-not $match.Success) {
        throw "Unable to parse rt performance output from '$Bundle'"
    }
    return [double]::Parse($match.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
}

function Get-Median {
    param([Parameter(Mandatory = $true)][double[]]$Values)

    $sorted = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) {
        return $sorted[$middle]
    }
    return ($sorted[$middle - 1] + $sorted[$middle]) / 2.0
}

function Read-VerifiedMetadata {
    param([Parameter(Mandatory = $true)][string]$Bundle)

    $metadataPath = Join-Path $Bundle 'metadata.json'
    if (-not (Test-Path -LiteralPath $metadataPath -PathType Leaf)) {
        throw "Runtime metadata is missing: $metadataPath"
    }
    $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
    if ($metadata.schema -ne 1) {
        throw "Unsupported runtime metadata schema in '$metadataPath'"
    }
    $runtimeFiles = @($metadata.runtime_files)
    if ($runtimeFiles.Count -eq 0) {
        throw "Runtime metadata contains no files: $metadataPath"
    }
    if (@($runtimeFiles.name | Sort-Object -Unique).Count -ne $runtimeFiles.Count) {
        throw "Runtime metadata contains duplicate file names: $metadataPath"
    }

    foreach ($record in $runtimeFiles) {
        if ((Split-Path -Leaf $record.name) -ne $record.name) {
            throw "Runtime metadata contains an invalid file name: $($record.name)"
        }
        $path = Join-Path $Bundle $record.name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Runtime file is missing: $path"
        }
        $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        if ($actualHash -ne $record.sha256) {
            throw "Runtime file hash does not match metadata: $path"
        }
    }
    return $metadata
}

function Invoke-CorrectnessComparison {
    param(
        [Parameter(Mandatory = $true)][string]$OutputDirectory,
        [Parameter(Mandatory = $true)][string]$BaselineResult,
        [Parameter(Mandatory = $true)][string]$CandidateResult,
        [Parameter(Mandatory = $true)][ValidateSet('shotline', 'edge')]
        [string]$Kind
    )

    $comparisonArguments = @(
        '-c', '--input-rays', $canonicalRays,
        '--output-status', (Join-Path $OutputDirectory "$Kind.comparison.json"),
        '--output-nirt', (Join-Path $OutputDirectory "$Kind.diff.nrt"),
        '-t', $comparisonToleranceArgument
    )
    if ($Kind -eq 'edge') {
        $comparisonArguments += @(
            '--grazing-geometry', $geometry,
            '--grazing-object', $Object
        )
    }
    $comparisonArguments += @(
        $BaselineResult,
        $CandidateResult
    )

    Invoke-Rtcmp $baselineBundle $comparisonArguments @(0, 6) | Out-Null

    return Get-Content -LiteralPath (Join-Path $OutputDirectory "$Kind.comparison.json") `
        -Raw | ConvertFrom-Json
}

if (-not (Test-Path -LiteralPath $runtimeDirectory -PathType Container)) {
    throw "Runtime directory does not exist: $runtimeDirectory"
}
if (-not (Test-Path -LiteralPath $geometry -PathType Leaf)) {
    throw "Geometry does not exist: $geometry"
}
if (-not (Test-Path -LiteralPath $performanceGeometry -PathType Leaf)) {
    throw "Performance geometry does not exist: $performanceGeometry"
}
if (Test-Path -LiteralPath $runDirectory) {
    throw "Refusing to overwrite run directory: $runDirectory"
}
if (@($Releases | Sort-Object -Unique).Count -ne $Releases.Count) {
    throw 'Release names must be unique'
}
$metadataByName = @{}
foreach ($name in @('main') + $Releases) {
    $bundle = Join-Path $runtimeDirectory $name
    if (-not (Test-Path -LiteralPath (Join-Path $bundle 'rtcmp.exe') -PathType Leaf)) {
        throw "Runtime bundle is incomplete: $bundle"
    }
    $metadataByName[$name] = Read-VerifiedMetadata $bundle
}

$baselineMetadata = $metadataByName.main
foreach ($release in $Releases) {
    $candidateMetadata = $metadataByName[$release]
    if ($candidateMetadata.rtcmp_source_fingerprint -ne $baselineMetadata.rtcmp_source_fingerprint) {
        throw "rtcmp source fingerprint differs between main and $release"
    }
    if ($candidateMetadata.build_type -ne $baselineMetadata.build_type) {
        throw "Build type differs between main and $release"
    }
    if ($candidateMetadata.runtime_debug -ne $baselineMetadata.runtime_debug) {
        throw "Runtime-debug setting differs between main and $release"
    }
    if ($candidateMetadata.brlcad_ref -ne "refs/tags/$release") {
        throw "Runtime bundle '$release' records unexpected ref '$($candidateMetadata.brlcad_ref)'"
    }
}

New-Item -ItemType Directory -Path $baselineDirectory, $resultsDirectory -Force | Out-Null
$baselineShotline = Join-Path $baselineDirectory 'shotline.json'
$baselineGenerationStatusPath = Join-Path $baselineDirectory 'shotline.generation.json'
Invoke-Rtcmp $baselineBundle @(
    '-d', '--minimum-rays', $MinimumRays,
    '--output-rays', $canonicalRays,
    '--output-json', $baselineShotline,
    '--output-status', $baselineGenerationStatusPath,
    $geometry, $Object
) @(0) | Out-Null
$baselineGenerationStatus = Get-Content -LiteralPath $baselineGenerationStatusPath -Raw |
    ConvertFrom-Json

$summary = foreach ($release in $Releases) {
    $candidateBundle = Join-Path $runtimeDirectory $release
    $candidateMetadata = $metadataByName[$release]
    $releaseDirectory = Join-Path $resultsDirectory $release
    New-Item -ItemType Directory -Path $releaseDirectory -Force | Out-Null

    $candidateResult = Join-Path $releaseDirectory 'shotline.candidate.json'
    Invoke-Rtcmp $candidateBundle @(
        '-d', '--input-rays', $canonicalRays,
        '--output-json', $candidateResult,
        '--output-status', (Join-Path $releaseDirectory 'shotline.generation.json'),
        $geometry, $Object
    ) @(0) | Out-Null
    $shotlineStatus = Invoke-CorrectnessComparison `
        -OutputDirectory $releaseDirectory `
        -BaselineResult $baselineShotline `
        -CandidateResult $candidateResult `
        -Kind shotline
    $edgeStatus = Invoke-CorrectnessComparison `
        -OutputDirectory $releaseDirectory `
        -BaselineResult $baselineShotline `
        -CandidateResult $candidateResult `
        -Kind edge
    Remove-Item -LiteralPath $candidateResult -Force

    $baselineSamples = @()
    $candidateSamples = @()
    for ($sample = 0; $sample -lt $PerformanceRepetitions; ++$sample) {
        if (($sample % 2) -eq 0) {
            $baselineSamples += Invoke-PerformanceSample $baselineBundle
            $candidateSamples += Invoke-PerformanceSample $candidateBundle
        } else {
            $candidateSamples += Invoke-PerformanceSample $candidateBundle
            $baselineSamples += Invoke-PerformanceSample $baselineBundle
        }
    }
    $baselineMedian = Get-Median $baselineSamples
    $candidateMedian = Get-Median $candidateSamples
    if ($baselineMedian -le 0.0 -or [double]::IsNaN($baselineMedian) `
        -or [double]::IsInfinity($baselineMedian)) {
        throw "Main produced an invalid performance median while comparing $release"
    }
    if ($candidateMedian -le 0.0 -or [double]::IsNaN($candidateMedian) `
        -or [double]::IsInfinity($candidateMedian)) {
        throw "$release produced an invalid performance median"
    }
    $ratio = $candidateMedian / $baselineMedian
    $performance = [ordered]@{
        seconds_per_sample = $PerformanceSeconds
        repetitions = $PerformanceRepetitions
        max_ray_pool_bytes = $PerformanceMaxMemoryBytes
        cpus = 1
        baseline_rays_per_second = $baselineSamples
        candidate_rays_per_second = $candidateSamples
        baseline_median = $baselineMedian
        candidate_median = $candidateMedian
        candidate_to_baseline_ratio = $ratio
        candidate_delta_percent = ($ratio - 1.0) * 100.0
    }
    $performance | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $releaseDirectory 'performance.json') -Encoding UTF8

    [pscustomobject][ordered]@{
        release = $release
        commit = $candidateMetadata.brlcad_commit
        shotline_state = $shotlineStatus.state
        shotline_different_rays = $shotlineStatus.mismatches.different_rays
        edge_state = $edgeStatus.state
        edge_different_rays = $edgeStatus.mismatches.different_rays
        edge_filtered_grazing = $edgeStatus.mismatches.filtered_grazing
        baseline_median_rays_per_second = $baselineMedian
        candidate_median_rays_per_second = $candidateMedian
        candidate_to_baseline_ratio = $ratio
        candidate_delta_percent = ($ratio - 1.0) * 100.0
    }
}

$summary | Export-Csv -LiteralPath (Join-Path $runDirectory 'summary.csv') -NoTypeInformation
$processor = $env:PROCESSOR_IDENTIFIER
if (-not $processor) {
    $processor = Get-ItemPropertyValue `
        -LiteralPath 'Registry::HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0' `
        -Name ProcessorNameString `
        -ErrorAction SilentlyContinue
}
$processArchitecture = if ([Environment]::Is64BitProcess) { 'x64' } else { 'x86' }
[ordered]@{
    schema = 1
    started_utc = $runStartedUtc
    completed_utc = [DateTimeOffset]::UtcNow.ToString('o')
    host = [ordered]@{
        computer_name = [Environment]::MachineName
        operating_system = [Environment]::OSVersion.VersionString
        processor = $processor
        process_architecture = $processArchitecture
        logical_processor_count = [Environment]::ProcessorCount
    }
    baseline_ref = $baselineMetadata.brlcad_ref
    baseline_commit = $baselineMetadata.brlcad_commit
    rtcmp_source_fingerprint = $baselineMetadata.rtcmp_source_fingerprint
    geometry = $geometry
    geometry_sha256 = (Get-FileHash -LiteralPath $geometry -Algorithm SHA256).Hash.ToLowerInvariant()
    object = $Object
    minimum_requested_rays = $MinimumRays
    actual_ray_count = $baselineGenerationStatus.actual_count
    rayset_hash = $baselineGenerationStatus.rayset_hash
    comparison_tolerance = $ComparisonTolerance
    comparisons = @('shotline', 'edge')
    edge_detector_runtime_ref = $baselineMetadata.brlcad_ref
    edge_detector_runtime_commit = $baselineMetadata.brlcad_commit
    performance_seconds = $PerformanceSeconds
    performance_repetitions = $PerformanceRepetitions
    performance_max_memory_bytes = $PerformanceMaxMemoryBytes
    performance_geometry = $performanceGeometry
    performance_geometry_sha256 = (Get-FileHash `
            -LiteralPath $performanceGeometry -Algorithm SHA256).Hash.ToLowerInvariant()
    performance_object = $performanceObject
    releases = $Releases
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runDirectory 'manifest.json') -Encoding UTF8

$summary | Format-Table -AutoSize

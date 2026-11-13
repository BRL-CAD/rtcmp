# Performance Runner Handoff

## Objective

Build an accurate, repeatable, auditable runner that compares two `rtcmp`
builds over discovered `.g` + object cases.  Correctness gates performance.
Both builds must shoot the same canonical ordered rays, and performance results
must stabilize before receiving a directional classification.

This document is the implementation contract for fresh agents.  Work through
the stages in order.  Keep each stage a focused, manually reviewable diff with
its own tests.  Preserve unrelated working-tree changes.

## Starting Procedure

1. Read this document completely.
2. Inspect `git status --short` and the complete diffs before editing.  The
   working tree may contain uncommitted work from earlier agents.
3. Inspect the current implementations in:
   - `perf.sh`
   - `perf/perfcomp.cpp`
   - `comp/jsoncmp.cpp`
   - `comp/jsonwriter.hpp`
   - `comp/shot_comp.cpp`
   - `util/util.cpp`
   - `rtcmp.cpp`
   - `rtcmp.h`
   - `tests/harness_cleanup_test.sh`
4. Implement only the current stage.  Do not begin a later stage to hide an
   incomplete contract in the current one.
5. Run the narrowest relevant tests, then syntax/build/integration checks.
6. Update this handoff only when an implementation detail changes the contract
   or a stage is completed.  Record completed stages and remaining work in
   **Progress**.

## Non-Negotiable Invariants

- Build 1 is the **baseline**.  Build 2 is the **candidate**.
- Every ratio is `candidate / baseline`; positive percentage means candidate
  improvement.
- Baseline and candidate shoot the exact same ordered canonical rayset for a
  case, across correctness, every performance pair, and run resume.
- Correctness accounts for every canonical ray exactly once.  Unknown,
  missing, duplicate, malformed, or incomplete results fail correctness.
- Correctness uses configurable geometric tolerance, not bitwise result
  equality.
- A normal performance result is emitted only after correctness succeeds.
  Explicitly skipped correctness is always labeled `correctness_unchecked`.
- Timed throughput excludes scene setup, rayset loading, and warmup.
- Each performance process traverses the complete resident ray pool once
  before the timed loop.
- Samples are paired, commands alternate first position, and active cases are
  deterministically shuffled between rounds.
- Samples are never silently discarded as outliers.  Noise changes confidence
  and classification rather than the underlying observations.
- Setup timing is observational and never blocks throughput classification.
- Strict suite results require every discovered case to be correct and stable.
  Partial aggregates are separately named and never presented as strict.

## Component Boundary

Use a compiled case engine with a lightweight shell scheduler.

### Compiled code owns

- Strict canonical rayset generation, reading, validation, hashing, and writing.
- Correctness result coverage and tolerance-aware comparison.
- Meaningful operation exit statuses and compact structured status output.
- Canonical ray replay, one-pool warmup, and one timed performance sample.
- Paired statistics, drift checks, classifications, suite aggregation, and
  authoritative JSON/CSV serialization.

### `perf.sh` owns

- MGED model/object discovery.
- Process launch, isolated runtime environments, output capture, and timeouts.
- Correctness workflow sequencing.
- Balanced pair order, deterministic active-case scheduling, and stop budgets.
- Run creation/resume, output layout, artifact retention, and cleanup.
- Mapping final suite state to the process exit code.

The shell should invoke compiled transformations instead of scraping prose or
reimplementing statistics.  Do not move MGED interaction, cross-platform
subprocess control, or suite scheduling into C++ in these stages.

## Inputs and Build Isolation

The public runner inputs are:

- `RTCMP1_ROOT`: baseline build/install root.
- `RTCMP2_ROOT`: candidate build/install root.
- `RTCMP_DRIVER`: compiled analysis/report executable.  Its hash is part of
  run identity.
- `RAY_GENERATOR`: canonical generator executable; defaults to the resolved
  baseline `rtcmp` executable.
- `MGED`: explicit discovery executable; defaults to MGED resolved from
  `RTCMP1_ROOT`.
- `RTCMP_ARGS`: shared `rtcmp` arguments.
- `RTCMP1_ARGS`: baseline-only arguments.
- `RTCMP2_ARGS`: candidate-only arguments.

Resolve exact executable paths before starting.  Launch each measured command
with only its own root prepended to relevant binary/library/data search paths.
Baseline paths must not leak into candidate execution, or vice versa.  Reject
extra arguments that override runner-controlled ray input, duration, output,
or measurement settings.

Default `NUM_CPUS=1`.  Record overrides, and do not describe the current serial
shooting loop as multithreaded.  An optional affinity wrapper may be supported.

## Canonical Rayset Contract

Reuse and extract the existing `.rays` mechanism from `comp/jsoncmp.cpp` rather
than adapting shot-result JSON into a replay format.

For `PERF_MIN_RAY_COUNT=100000`, generate the smallest balanced six-view grid
whose actual total is at least the requested minimum.  The existing generator
may round upward to its grid shape.  Record:

- Requested minimum.
- Per-view count.
- Actual total count.
- Exact `.g` identity and object name.
- Generator executable identity.
- Deterministic rayset content hash.

The strict reader/writer must:

- Recognize the expected header and declared count.
- Require one complete origin/direction pair per ray in canonical order.
- Use round-trippable floating-point serialization.
- Reject non-finite values, unknown records, malformed records, trailing
  records, count mismatches, and exact duplicate six-component rays.
- Produce the same content hash after write/read round trip.

Global result output order need not match ray order.  Identity and coverage are
by canonical ray key/hash.

`RAY_GENERATOR` is authoritative for ray generation.  When its executable hash
matches the baseline executable hash, its generated baseline correctness
result may be reused.  Otherwise both baseline and candidate replay the
generated rayset for correctness.

Successful raysets are deleted after a completed run unless explicitly kept.
Interrupted-run raysets and failure raysets remain available for resume or
diagnosis.

## Correctness Contract

Normal `all` mode performs, per case:

1. Generate and strictly validate canonical rays.
2. Produce baseline results for those rays.
3. Replay the same rays through the candidate.
4. Compare baseline and candidate with `CORRECTNESS_TOLERANCE`, defaulting to
   the existing BRL-CAD comparison tolerance.
5. Require exactly one valid result, including misses, for every canonical ray.
6. Write compact structured status with expected/observed counts, rayset hash,
   mismatch classes, tolerance, and final state.

Region names and partition counts remain exact.  Floating-point points,
normals, and distances use the configured tolerance.

Replace text matching such as `No differences found` with meaningful exits and
structured status.  Individual compiled operations need distinct nonzero
statuses for usage/configuration, I/O, scene setup, invalid rayset, correctness
mismatch, and incomplete/malformed coverage.

Correctness subprocesses default to five-minute timeouts.  The complete
correctness workflow defaults to a fifteen-minute per-case budget.  Preserve
all failure artifacts.  Large successful baseline/candidate shot results and
other successful intermediates may be removed after compact status is durable.

## Performance Sample Contract

A measured invocation must:

1. Construct/prepare the scene and record `scene_setup_seconds`.
2. Strictly load the canonical rayset outside setup and timed throughput.
3. Report loaded ray count and identity for auditing.
4. Traverse the complete pool once as an untimed in-process warmup.
5. Reset measurement counters.
6. Shoot sequentially through the pool, wrapping as needed, for
   `PERF_SECONDS`, default `10`.
7. Emit structured sample output atomically.

Each sample records at least:

- Baseline/candidate role and executable identity.
- Case identity and rayset hash/count.
- Setup seconds.
- Measured wall and CPU seconds.
- Rays shot, rays per wall second, pool count, and pool reuse.
- Process elapsed time and exit/failure classification.

Rayset load time may be retained as diagnostics but is not a comparison metric.
Setup raw values and derived median/min/max/CV are reported.  Setup does not
participate in throughput stabilization.

## Adaptive Throughput Contract

For each valid adjacent pair, calculate:

```text
x = log(candidate_rays_per_second / baseline_rays_per_second)
```

The compiled analyzer must calculate:

- Point estimate: `exp(mean(x))`.
- Sample standard deviation of paired log ratios.
- Student-t 95% confidence interval for the mean, transformed back to a ratio.
- Median paired ratio and MAD as diagnostics.
- Per-command median/min/max/CV as diagnostics.
- First-half versus second-half geometric-mean drift.

Defaults:

- Minimum valid pairs: `8`.
- Maximum valid pairs: `24`.
- Maximum total attempted measurement time per case: `300` seconds.
- Confidence level: `95%`.
- Stability tolerance: `1%`, configurable.
- Practical significance threshold: `1%`, independently configurable.

Re-evaluate after every new valid pair.  A case stabilizes only when:

- It has at least eight valid pairs.
- Maximum relative confidence-interval half-width is within the stability
  tolerance.
- First-half and second-half geometric means differ by no more than the
  stability tolerance.

Stop when stable, when 24 valid pairs are reached, or when 300 total attempted
seconds are consumed, whichever occurs first.  Failed and timed-out attempts
count toward elapsed budget but not valid-pair count.  Record both commands'
attempts when possible.  Deterministic incompatibilities stop the case
immediately.

Throughput classifications:

- `improved`: the complete stable confidence interval is above the positive
  practical threshold.
- `regressed`: the complete stable confidence interval is below the negative
  practical threshold.
- `no_change`: stable, but not wholly beyond either practical threshold.
- `unstable`: valid measurements reached a cap without satisfying stability.
- `incorrect`: correctness failed.
- `failed`: execution/infrastructure prevented a valid result.
- `correctness_unchecked`: performance intentionally ran without the gate.

## Modes

- `all`: correctness gate followed by performance; default.
- `correctness`: canonical generation/replay and comparison only.
- `performance`: use a supplied/predefined canonical rayset without running the
  gate and label all results `correctness_unchecked`.

`--skip-correctness` is an explicit manual escape hatch equivalent to unchecked
performance semantics.  Stable unchecked results may aggregate, but their
status remains visibly different from correctness-gated results.

## Scheduling

- Discover cases once with explicit/defaulted MGED.
- Run correctness before admitting a case to measured scheduling.
- Deterministically shuffle only active cases each round.
- Give each active case one adjacent baseline/candidate pair.
- Alternate which role runs first for every case across pairs.
- Remove stable, incorrect, failed, or capped cases from the active set.
- Support a configurable pause without making it part of measured time.

## Run State and Output

Use a stable layout equivalent to:

```text
out/
  rays/
  runs/<run-id>/
    run-manifest.json
    correctness/
      baseline/
      candidate/
      comparisons/
    perf/
      logs/
      samples/
      cases/
    summary.json
    summary.csv
```

JSON is authoritative.  CSV is a human-readable projection.

The manifest records normalized configuration, run ID, completion state,
resolved paths/hashes, runner/script hash, repository revision and dirty state,
model/case identities, MGED identity, rayset settings, timing/statistical
thresholds, CPU/affinity settings, hostname, OS, CPU, memory, locale, relevant
library/data paths, and benchmark-specific environment values.  Use a deliberate
environment whitelist; do not dump unrestricted credentials or unrelated
environment state.

An incomplete manifest resumes only when all run-identity fields match exactly.
Any mismatch restarts in a new run directory rather than mixing observations.
Correctness reruns for every fresh run but not for a validated interrupted
resume.  Write sample artifacts immutably and regenerate case state from them;
do not rely on one incrementally edited state file as the sole record.

Finalization order:

1. Atomically write authoritative JSON and CSV summaries.
2. Mark the manifest complete as the final state update.
3. Remove successful transient artifacts and unrequested raysets.
4. Report cleanup failures as warnings without invalidating completed results.

## Suite Results and Exit Policy

Give each case equal weight.  Calculate suite geometric means from case ratios;
do not weight by runtime or ray count.  Report:

- Strict aggregate only when every case is correct and stable.
- Separately labeled partial aggregate when any case is excluded.
- Propagated aggregate measurement confidence from per-case log-ratio standard
  errors.
- Suite classification using the configured practical threshold.
- Counts for every case classification.

Final process status communicates the highest-priority suite condition; JSON
contains every case status.  Initial mapping:

- `0`: completed and stable, including explicit unchecked mode unless policy
  requests otherwise.
- `2`: usage/configuration error.
- `3`: incorrect result/correctness failure.
- `4`: unstable or incomplete suite.
- `5`: execution/infrastructure failure.
- `6`: stable regression only when regression-failure policy is enabled.

Priority is configuration/infrastructure, incorrect, failed, unstable,
optional regression, then success.  A stable regression is normally a valid
measurement and returns success.  Support a policy such as
`FAIL_ON_REGRESSION=1` for CI enforcement.

## Staged Implementation

### Stage 1: Strict rays and correctness

Scope:

- Extract strict rayset I/O/generation into a shared compiled module.
- Add minimum balanced ray-count generation and rayset hashing.
- Harden correctness result parsing and exact coverage accounting.
- Return meaningful correctness exits and compact structured status.
- Preserve existing comparison tolerance behavior.

Likely files include `comp/jsoncmp.cpp`, `comp/shot_comp.cpp`,
`comp/shot_comp.h`, `comp/compare_config.h`, `util/util.cpp`, `rtcmp.cpp`,
`rtcmp.h`, CMake files, and focused new rayset/correctness tests.

Completion gate:

- Strict round-trip succeeds.
- Malformed header, bad ordering, non-finite values, duplicates, missing/extra
  records, and count mismatches fail deterministically.
- Correctness distinguishes equivalent, different, malformed, incomplete, and
  I/O-failure cases through status plus exit code.

### Stage 2: Canonical performance replay

Scope:

- Replace adaptive per-build ray-pool generation with strict canonical replay.
- Add complete-pool in-process warmup.
- Keep setup timing distinct and time only shooting.
- Emit atomic structured sample output with rayset identity.

Completion gate:

- Both builds report the same rayset identity/count.
- Same-build replay measures the same ordered workload.
- Warmup counters do not contaminate measured counters.
- Invalid replay cannot produce a valid performance sample.

### Stage 3: Compiled statistics and reporting

Scope:

- Implement paired log-ratio statistics, Student-t interval, drift check,
  diagnostics, classifications, and suite aggregation.
- Emit authoritative per-case/suite JSON and CSV projection.
- Add golden fixtures for improved, regressed, no-change, noisy, and drifting
  data.

Completion gate:

- Golden results match independently checked expected values.
- Classification boundaries at the practical threshold are tested.
- Unstable and drift cases remain unresolved at the configured cap.
- Strict and partial aggregate fixtures are correct.

### Stage 4: Lightweight orchestrator

Scope:

- Restructure `perf.sh` around root resolution, isolated process environments,
  modes, correctness admission, active-case rounds, timeouts, and compiled
  analysis calls.
- Stop parsing human-readable performance prose.
- Enforce output layout and artifact policy.

Completion gate:

- Order is balanced for every case.
- Settled cases receive no further samples.
- Failed attempts consume time budget without becoming valid pairs.
- Correctness failures never enter normal performance scheduling.
- `all`, `correctness`, and unchecked `performance` modes produce the expected
  statuses.

### Stage 5: Resume, provenance, and integration

Scope:

- Add atomic run manifest lifecycle and exact-match resume.
- Capture approved machine/build/environment provenance.
- Finalize strict/partial summaries, cleanup, and suite exit mapping.
- Register focused C++ tests and shell integration coverage with CTest when
  available.

Completion gate:

- Interrupted runs resume without repeating completed pairs.
- Manifest mismatch refuses to merge samples.
- Build environments remain isolated.
- Timeout and failure runs terminate within configured budgets and retain
  diagnostics.
- Successful finalization leaves durable summaries and completed manifest even
  when cleanup warns.

## Required Acceptance Coverage

- Strict rayset read/write and every rejection class in Stage 1.
- Complete correctness coverage with no unknown/missing/duplicate results.
- Meaningful operation exits; no correctness decision based on prose matching.
- Identical replay identity across baseline and candidate.
- Same-build end-to-end run classifies `no_change` when stable.
- Synthetic stable positive/negative effects classify around the 1% threshold.
- Noisy and drifting fixtures continue sampling and cap as `unstable`.
- Balanced command ordering and active-case removal.
- Per-process and per-case timeout behavior.
- Unchecked mode remains visibly `correctness_unchecked`.
- Interrupted resume and manifest-mismatch restart.
- Strict and partial suite aggregates.
- Runtime path isolation between baseline and candidate roots.

## Explicit Non-Goals

- Hashing every dynamically loaded dependency in the first implementation.
- Controlling every OS power-management or background-load setting.
- Multithreaded shooting in the current performance loop.
- Aggregating unrelated model/object raysets into one file.
- Supporting legacy builds that cannot load and audit canonical performance
  raysets.
- A full cross-platform C++ process/scheduling framework.

## Progress

- Design grilling: complete and confirmed.
- Stage 1: complete (2026-10-02).
  - Strict shared rayset I/O preserves the `.rays` record shape, writes
    round-trippable values, and identifies canonical ordered numeric content
    with `sha256:<hex>` independently of the output path.
  - `--minimum-rays` selects the smallest complete balanced six-view pool at
    or above a requested total; legacy `--rays-per-view` remains available.
  - `--output-status` writes the same compact JSON status emitted on stdout.
    Compiled-operation exits are `2` usage/configuration, `3` I/O, `4` scene
    setup, `5` invalid rayset, `6` correctness mismatch, and `7` malformed or
    incomplete result coverage (`0` remains success and `1` general execution
    failure).
  - CTest coverage exercises strict round-trip/hash and every required rayset
    rejection class, plus equivalent, different, malformed, incomplete,
    duplicate, unknown, non-finite, and I/O correctness outcomes.  A same-build
    canonical replay smoke test and invalid-scene exit check also pass.
  - Executable-level configuration failures replace requested status output
    with structured JSON, equivalent comparisons remove stale difference-ray
    output, and the legacy harness retains correctness evidence on failure.
  - CTest supplies the BRL-CAD runtime path on Windows.  Runtime loading, CLI
    status, and harness artifact policy have focused regression tests.
  - Resolved executable and model hashes remain Stage 5 manifest provenance;
    Stage 1 status fields record the supplied generator and geometry labels.
- Stage 2: not started.
- Stage 3: not started.
- Stage 4: not started.
- Stage 5: not started.


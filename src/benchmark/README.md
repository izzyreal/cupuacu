# Performance benchmarks

These benchmarks exercise Cupuacu's current document, undo, file loading,
waveform and event-loop code. Use them before changing storage or scheduling
and keep the resulting JSON as a baseline. Benchmark targets are opt-in and
are not part of ordinary builds or the unit test suite.

## Build and run

Use Release without coverage or realtime sanitizers:

```sh
cmake -S . -B build-benchmark -DCMAKE_BUILD_TYPE=Release \
  -DCUPUACU_BUILD_BENCHMARKS=ON \
  -DCUPUACU_ENABLE_COVERAGE=OFF -DCUPUACU_ENABLE_RTSAN_LIBS=OFF
cmake --build build-benchmark --target cupuacu-benchmarks cupuacu-benchmarks-metrics -j4
python3 scripts/test_benchmark_runner.py
python3 scripts/run-benchmarks.py --build-dir build-benchmark \
  --output dist/benchmarks/before.json
```

For multi-configuration generators, build Release and pass its executable
directory to `--build-dir`. Run on an idle machine after compilation finishes.
Repeat the same command after an incremental build, adding
`--compare dist/benchmarks/before.json --output dist/benchmarks/after.json`.
JSON contains raw repetitions, summary statistics, adjacent-size scaling
ratios, environment and source fingerprints, and comparison results. A sibling
Markdown file gives a compact overview. Host, compiler, renderer, dependency,
or fixture changes make comparisons explicitly incompatible. Failed and missing
cases remain visible; timing changes do not cause a failing exit status.

For performance iterations, keep one native Release build directory and build
only the timing executable. It shares `cupuacu_core` with the app; the diagnostic
executable uses a separately compiled core with counters enabled. For example,
in the existing configured `build` directory:

```sh
time cmake --build build --target cupuacu-benchmarks -j4
python3 scripts/run-benchmarks.py --build-dir build --mode timing \
  --filter 'open_uncached' --sizes-mib 1 256 --repetitions 3 \
  --output dist/benchmarks/open-before.json
```

`--mode timing` needs no diagnostic executable; `--mode diagnostic` selects
counter measurements, and the default `--mode all` preserves combined runs.
Preserve the baseline executable and reuse fixtures. Measure one affected
scenario at a small and larger size, adding repetitions only when noise prevents
a conclusion. Run focused correctness tests for changed behavior. Build the app
when needed for manual verification; reserve broader tests and cross-platform
checks for demonstrated performance checkpoints. Do not clean, reconfigure, or
switch platforms during the routine loop. Investigate unexpected compilation
instead of accepting it as normal iteration cost.

`--filter '*gain*'`, `--sizes-mib 1 4 16`, and `--repetitions 3` select smaller
runs. `--profile extended` adds 64/256 MiB inputs, copy/paste/trim, unshared
sample editing, dirty-cache navigation, different delete positions, FLAC
opening, ALAC M4A opening in both core and SDL suites, and histories of 1/8/32
fixed-size edits on a 4 MiB document. ALAC fixtures encode the same deterministic
16-bit samples as WAV/FLAC; sizes always describe decoded float audio.

Quick core runs target roughly one minute, excluding build and fixture
generation. They use 1/4/16 MiB of **decoded stereo float samples**, three
timing repetitions and one diagnostic repetition. Setup and full correctness
validation add wall time outside the measured operation. Slow runs are reported
without silently dropping workloads. Large runs require explicit budgets:

```sh
python3 scripts/run-benchmarks.py --build-dir build-benchmark \
  --profile large --sizes-mib 512 2048 --filter 'open_*' --repetitions 3 \
  --max-rss-mib 8192 --max-disk-mib 16384 --timeout-seconds 300 \
  --output dist/benchmarks/large.json
```

The runner samples child RSS and kills over-budget or timed-out processes. It
also checks their final lifetime peak RSS. Disk limits cover the fixture cache
and each temporary scenario directory separately, not a filesystem quota;
sampling cannot prevent a brief overshoot. Defaults are 2 GiB RSS, 4 GiB disk
and 120 seconds per process. Reused fixtures live in `build/benchmark-fixtures`
unless `--fixtures` overrides this. Scenario files use private temporary
directories and are removed after each repetition.

## Linux rendering and responsiveness

SDL benchmarks require Linux, Xvfb, X11 and SDL's software renderer, matching
the existing integration test environment. They build actual editor windows
with a 1024-pixel waveform viewport and drive the shared application loop.

```sh
docker build -t cupuacu-benchmark-linux -f docker/integration/linux/Dockerfile .
docker run --rm --hostname "$(hostname)" -v "$PWD:/work" -w /work cupuacu-benchmark-linux \
  /bin/bash scripts/run-benchmarks-linux.sh \
  --output dist/benchmarks/linux.json
```

`scripts/run-benchmarks-linux.sh` builds all three executables and runs both
core and SDL cases. Pass `--suite sdl` to select rendering alone. The manual
`benchmark` pipeline in `ciwi-project.yaml` publishes reports as artifacts and
fails on correctness, harness or resource failures, without timing gates.
Use a stable hostname tied to the host machine when comparing container runs.

Navigation exercises real key dispatch, sample-level views, block views and
fit-to-file views. Dirty navigation starts with a structural edit whose cache
work is pending. The core navigation cases measure waveform queries; SDL cases
also wait for the current waveform texture. They are different measurements
and should not be compared directly.

An independent producer schedules event probes every 2 ms. Their timestamps
retain queueing delays, including delays inside synchronous commands and nested
long-task event pumps. `responsive_stall` deliberately blocks for 80 ms and
must detect at least 50 ms of latency. `responsive_gain` probes a real whole-file
effect. SDL probes also count navigation events allowed or blocked by the
long-task gate; being allowed by that gate does not prove a visible scroll.
The report emits p95 only with at least 100 observations and p99 only with at
least 1000. Short cases usually provide a maximum and count instead.

Opening cases now collect event probes too, including in the core suite.
`event_loop.max_iteration_ms` records the longest application-loop iteration;
core runs omit drawing, so use SDL runs or native profiling for rendering stalls.
The probe overhead is included in opening timings and should be present on both
sides of a timing comparison. Earlier reports without opening probes are not
equivalent measurements.

Background opening builds peaks from each decoded block and publishes them
through an eight-batch queue. The UI consumes at most eight batches or three
milliseconds of application work per iteration. It displays a read-only preview
at the full file duration; decoding retains ownership of the audio until success.
Cancel or failure restores the previous tabs. Preview edge windows use base
peaks, without reading unavailable audio. Final document queries remain exact.
Persistent peak-cache hits bypass generation and can display the saved waveform
during decoding. This does not yet impose a decoded-audio RAM limit.

`first_waveform` records the first available nonempty peak prefix in the active
session. In core benchmarks this measures availability, not pixels presented on
screen. `audio_available` and `committed` exclude the preview and still describe
the completed, editable document.

Initial waveform-cache persistence uses a worker with at most four pending
requests and one active write. Requests share immutable peak pages and hold no
audio. A full queue is retried; an audio edit invalidates the pending retry.
`waveform_complete` can precede cache-file completion. `background_complete`
still includes that disk work, and the benchmark keeps pumping events while it
waits. No file-format or clipboard-persistence behavior changes.

Exact overview queries combine finer cached peaks at coarse-peak boundaries.
For a completely built cache, raw scanning is limited to fewer than 256 samples
per query, regardless of zoom level. Dirty/unbuilt ranges retain exact raw
fallback. Navigation's `waveform_queries` counters expose this work separately
from rendering and disk persistence. The extended `zoom_unaligned` case uses
fractional fit-to-file windows to exercise coarse-peak boundaries that ordinary
power-of-two fixtures and viewport widths would otherwise miss.
While an overview cache is being built, rendering clips queries to its published
prefix. This avoids an initial full-file raw scan before the first peaks arrive.
Sample-level rendering and exact non-rendering queries keep their raw fallback.

## Interpreting measurements

The timing executable links the ordinary core. The diagnostic executable links
a separately compiled core with atomic work counters and capacity observations;
its timing is not a substitute for uninstrumented timing. Google Benchmark
v1.9.5 supplies the benchmark driver, using one manually timed iteration per
fresh child. On Unix a pristine, single-threaded parent discovers CPU metadata
once, then forks before any SDL or document initialization. Windows spawns a
fresh executable. Documents, undo history, application caches and worker threads
are never reused across repetitions. OS filesystem caching is uncontrolled;
`open_uncached` means **no persistent Cupuacu peak cache**, not cold disk I/O.

Milestones are milliseconds from command submission:

| Field | Meaning |
|---|---|
| `command_return` | The command or complete navigation sequence returned. |
| `committed` | The new document/edit became available to the main loop. |
| `audio_available` | An opening document exposed its full decoded audio. |
| `first_waveform` | A nonempty waveform prefix became available; core runs do not measure screen presentation. |
| `view_ready` | SDL has a current waveform texture and no active long task. |
| `waveform_complete` | Both channel peak caches are clean. |
| `background_complete` | Resulting open/effect/cache/autosave work and scheduled clipboard writes drained. |

Unavailable milestones are null. Loop-observed milestones have event-loop
granularity; they are not internal decoder timestamps. Setup is outside these
timings. Opening cached fixtures builds their persistent peaks during setup.
Synthetic audio is populated deterministically with exact integer PCM values,
including noise, silence and transients; it is not a sparse or all-zero file.
Every result is validated against expected audio outside the measured interval.
Navigation also checks sampled peak windows against the expected samples.

Diagnostic counters observe explicit sample/metadata/peak copies, document
buffer clones, waveform/effect sample scans, base peaks rebuilt, and logical
undo, decoded audio, peak-cache and autosave bytes. These are deliberately named
work observations, not physical disk traffic or a complete allocator profiler.
They omit implicit vector-reallocation copies, some effect-specific scratch
buffers, and unrelated metadata operations. `waveform_queries` separately
reports raw sample scans and cached-peak use during viewport queries.
Diagnostic runs check a known explicit deep copy and the expected number of
base peaks during a complete cache rebuild, outside ordinary timing runs.
They also check that a peak snapshot copies no peak data, shared pages are
counted once, and editing one shared page records the actual copied bytes.

Tracked capacity covers observed audio vectors, preservation metadata, captured
segments, gain scratch buffers, and unique waveform peak pages and page tables.
Peak page-table copies contribute to `metadata_bytes_copied`; sharing a page
does not count as copying its peaks. Snapshot creation shares tables, while the
first write to a shared level copies its table of page pointers. That table
still grows with file length; peak-data copies are limited to touched pages.
Audio revisions similarly share pages of up to 16,384 samples per channel
(64 KiB of float samples), and integer PCM dirty bits use shared 4 KiB pages.
`AudioBuffer::snapshot()` shares these pages; `clone()` remains an explicit
deep sample copy and increments `full_buffer_clones`. Fixed gain and retained
single-sample diagnostics require zero full-buffer clones and at most 512 KiB
of sample copying. Page-table and provenance-range metadata can still grow
with document size or edit history. Structural sample shifting and full-file
autosave I/O remain; this storage is not yet disk-backed or RAM-budgeted.
Sequential consumers can copy channel blocks directly across sample pages,
including strided output for interleaving. These reads allocate no storage.
Sample-copy observations remain at the consuming operations; autosave packing
is represented by autosave I/O counters, not the bounded edit-copy budget.
Tracked capacity excludes allocator
overhead, SDL textures, codec buffers, thread stacks, outer-vector allocations
and transient reallocation overlap. It is not a hard memory bound. Process peak
RSS includes setup and all process allocations and is reported separately.
Neither metric describes filesystem page-cache residency.

Fixed 1000-frame edits and fixed-width viewports expose dependence on total
document size. Whole-file gain and cache construction provide expected linear
controls. Inspect deterministic work ratios first, then repeated timing and
memory measurements. Small cases can reveal full-buffer clones or full-cache
rebuilds; they cannot establish behavior under paging, storage saturation,
allocator thresholds, or multi-gigabyte histories. Validate promising changes
with the explicit large profile on target hardware. Audio-device callback
deadlines and real GPU performance require separate measurements.

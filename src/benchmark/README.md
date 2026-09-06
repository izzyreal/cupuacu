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
during decoding. Normal queued opening now writes decoded audio to owned disk blocks. Imports
share a sample-read cache capped at 10% of physical RAM; this does not bound all
process allocations. Peaks and indexes remain resident.

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

### Staged disk-backed audio

The extended core `open_owned` scenario exercises the new worker-only import
backend. It uses the existing decoders with an external sample sink, writes
immutable 65,536-frame channel blocks into 64 MiB segment files, and generates
waveform peaks from the same scratch blocks. Original source bytes are retained
independently: APFS cloning is attempted on macOS, with a cancellable 64 KiB
copy fallback. Source size/time changes during acquisition fail the import.

```sh
python3 scripts/run-benchmarks.py --build-dir build --mode timing \
  --profile extended --filter open_owned --sizes-mib 1 64 256 \
  --repetitions 3 --output dist/benchmarks/owned.json
```

This case fixes the decoded cache budget at 2 MiB and validates every sample
through bounded reads after timing import. `bounded_storage` reports cache
residency, logical block I/O bytes and 1,024-frame range-read timings. A cold
range means a miss in the decoded cache; the OS file cache is uncontrolled.
Warm reads must cause no block-file I/O. `source_cloned` records acquisition
mode. Import timing excludes validation and working-store reclamation.

The `AudioReader` interface is explicitly blocking and worker-only.
`DocumentAudioReader` pins the existing memory-backed revision during migration;
`AudioRevision` reads disk blocks through a shared bounded cache, and `AudioSlice`
retains a reference to a range. An import builder publishes a revision only at
successful completion. Final store release removes its owned working directory
and must occur on a worker. These process-local files are not a recovery format.

This backend now serves normal queued file opening; legacy synchronous loading,
resident restart histories and new-document creation retain compatibility paths. Its cache limit bounds decoded
payload residency, not total process memory: decoder/import scratch, the flat
block index and resident peak pyramid are separate. The cache exposes a 10%-of-
physical-RAM default calculation, but application-wide admission, preferences
and pressure handling remain to be integrated. Paged sequence indexes and remaining compatibility-path removal are subsequent
work. Revision sessions integrate transport and durable manifests. Existing codec frame-count limits still apply.
The external-sink metadata is rejected by the legacy document commit path to
prevent publishing a document without its samples.

`AsyncAudioReader` adds worker-owned range requests for a pinned revision. It
retains one pending request and one published result, rejects windows exceeding
its caller-supplied frame limit, and cancels obsolete requests between 65,536-frame
reads. Closing does not wait for disk I/O; the worker releases its reader reference
and any resulting working-store ownership. Consumers must bound results they
retain themselves. This is a per-revision service; global scheduling and memory
admission are still pending.

Ready document views now use the session viewport pipeline described below.
Progressive import and legacy cache-building views retain their existing
renderer, including its synchronous resident-sample fallbacks and geometry-worker
join. Disk readers are not connected to those fallbacks.

```sh
python3 scripts/run-benchmarks.py --build-dir build --mode timing \
  --profile extended --filter 'open_owned_viewport*' --sizes-mib 1 256 \
  --repetitions 3 --output dist/benchmarks/owned-viewport.json
```

These two cases compare synchronous and asynchronous range access using the same
64 viewport requests spread across the recording, varying window sizes from
1,024 to 131,072 frames. Each fresh process imports its own revision before timing,
with a 2 MiB decoded cache and a 512 KiB maximum result. `bounded_storage` reports
submission and completion latency separately, plus cache residency and logical
sample I/O. The synchronous control performs the same allocation and block reads
on the caller. Validation of every returned sample and final cleanup are excluded
from timing. OS caching is uncontrolled; this is neither a GUI event-latency
measurement nor a claim that dispatching work makes I/O itself faster. Completion
includes polling/thread scheduling overhead; imports remain outside this timer.

`AudioEditRevision` provides a persistent balanced sequence over imported block
ranges. Leaves retain the import revision, channel, source offset and length;
they never reference other edited revisions. Splitting/splicing shares unaffected
subtrees. Silence has no sample payload. `AudioEditTransaction` exposes erase,
insert/replace, trim and channel-specific replacement; a trimmed revision can
serve as a clipboard reference. Undo/redo can retain and switch immutable roots.
Source-range traversal preserves original-source provenance for later save
integration. Existing editor commands/history are not switched over by this step.

```sh
python3 scripts/run-benchmarks.py --build-dir build --mode timing \
  --profile extended --filter open_owned_edit --sizes-mib 1 256 \
  --repetitions 3 --output dist/benchmarks/owned-edit.json
```

This case imports real data, then fragments each channel with 1,024 reference
replacements outside timing. It measures 128 one-frame erase/insert transactions
and undo/redo reference switches, retaining their revisions. It checks that those
operations issue zero sample I/O, counts new index nodes per edit, and validates
every output sample afterward with bounded scratch. Additional warm 1,024- and
65,536-frame reads compare the fragmented tree to its unfragmented counterpart.
They report navigation overhead separately from edit timing. These are storage
operations, not complete UI commands (markers, dirty state, peaks, and persistence
are not included). Separate tests stress repeated inserts, randomized edit/history
sequences, 64-bit overflow and trillion-frame logical lengths using shared data;
those logical-length tests are not disk-throughput or real-file import tests.

Tree indexes are currently resident. A leaf pins its imported block directory
and packed store, so partial deletion does not reclaim individual source blocks.
Final release belongs on workers, as for imported revisions. Paging, per-block
reclamation, effects and default editor/history/save
integration remain pending. Cross-format pastes require a conversion step before
entering this transaction API.

Source peak pages now stay attached to imported revisions. The shared pyramid
is extended beyond the legacy 16-level ceiling to cover the source overview.
`AudioEditRevision::prepareWaveform` is worker-only: it builds exact aggregate
summaries for new tree nodes and skips prepared subtrees. New leaf boundaries
need at most 254 raw samples each; this preparation is separate from the edit
transaction, which still performs no sample I/O. Cold cache misses fetch full
decoded blocks, so sample counts do not equal physical read sizes. Cancellation leaves unfinished
summaries pending. Concurrent workers safely publish immutable cached results.

`queryWaveformOverview` reads only resident summaries. Fully covered tree nodes
return exact aggregates; partial pixels can expand to 128-frame source buckets,
clipped by exact edit-boundary peaks so deleted extrema cannot reappear. This
is an overview API; fine zoom must use asynchronous samples. Missing summaries
return pending, and already prepared subtrees can answer partial views while
ancestors are pending. Detailed peak paging and default disk-storage activation
remain pending; there is no sample-file fallback inside this query.

```sh
python3 scripts/run-benchmarks.py --build-dir build --mode timing \
  --profile extended --filter open_owned_waveform --sizes-mib 1 256 \
  --repetitions 3 --output dist/benchmarks/owned-waveform.json
```

This extends the fragmented edit case with a prepared starting tree. It times
boundary preparation separately, counts samples and new summaries, then measures
an unaligned 1,024-pixel stereo overview averaged over 16 warm queries. Every
sample in that viewport is checked against the returned min/max bounds after
timing. Overview queries must issue zero sample I/O. Initial tree preparation
is setup; the boundary preparation mean includes the first read with the decoded
cache in its post-setup state, and its maximum is also reported. These are headless
query timings, not measured GUI event or texture-upload latency.

`DocumentSession::getViewportSource` now supplies the normal ready-document GUI
with a cached immutable audio/peak snapshot. It also accepts an explicitly bound
disk edit revision through the same interface. This binding is staged: normal
opening still uses resident decoded audio, and editing, playback, save and recovery
must migrate before disk storage becomes the default. Marker-only changes reuse
the source; audio changes replace it without invalidating in-flight readers.

`WaveformViewport` prepares overview peaks, exact block-zoom peaks and padded
fine-zoom samples on a worker. Painting consumes published results, retaining
overscan textures and prefetching adjacent coverage. Missing data remains pending
without synchronous sample reads on this ready-view path. SDL geometry, texture
upload and spline evaluation remain on the UI thread. Requests are capped at
16,384 pixels; raw scratch is capped at `(16,385 * 128 + 8)` frames per channel.
Each view retains one pending request and one published result, with cancellation
between chunks. `AsyncAudioReader` shares the same latest-value worker. These are
per-view workers, not the planned application scheduler or global memory budget.

Session source/revision references use a background release queue, including
bindings that never created a view. Closing the new viewport worker does not join
or wait for blocked reads. This does not move all legacy document teardown off
the UI thread; the release queue itself drains during process shutdown.

```sh
python3 scripts/run-benchmarks.py --build-dir build --mode timing \
  --profile extended --filter '*_session' --sizes-mib 1 256 \
  --repetitions 3 --output dist/benchmarks/session-viewport.json
```

`open_memory_session` and `open_owned_session` exercise this shared session-to-
viewport path using resident and disk-backed audio respectively. Import and peak
construction occur outside timing. Each process requests 32 windows of 1,024
pixels across sample, block and overview zoom levels. Metrics separate initial
source creation, cached session lookup plus submission, completion including
worker scheduling/polling, and nonblocking close. Disk cases use a 2 MiB decoded
cache and report logical sample bytes read. Validation and waiting for final
worker shutdown are outside timing. These cases do not measure SDL drawing,
GUI event latency, cold-device latency or opening completion; process RSS still
includes setup. Compare repeated results and cache/work bounds, rather than
treating submillisecond completion differences as measured GUI speedups.

Comparisons with `open_uncached` are architectural references, not identical
operations: the owned path retains source bytes and writes decoded sample
storage; the legacy path retains decoded audio in RAM and persists its peaks.

Ordinary export now consumes a pinned `AudioReader`, including when called
through the existing document/save API. libsndfile exports use 65,536-frame bulk
reads. Native ALAC export quantizes and encodes 4,096-frame packets directly into
temporary output, then writes the packet table and chapter metadata. It no longer
assembles full-length PCM, encoded audio or M4A byte vectors. The packet index,
movie metadata and markers still grow with duration; codec scratch and decoded
cache residency are separate. Buffered codec/container helpers remain for callers
that explicitly request complete byte arrays, not the production export path.

M4A media sizes and chunk offsets can exceed 32 bits. The existing 32-bit ALAC
duration limit remains explicit: exports above `UINT32_MAX` frames are rejected
before reading samples. Original float-to-integer export quantization is unchanged;
provenance-aware preservation saves retain their separate implementation. WAV/AIFF
marker rewriting copies audio in 64 KiB chunks and is skipped for fresh exports
without markers. Writer errors/cancellation close handles and remove temporary
output; replacement no longer deletes the destination before attempting rename.
This is atomic replacement, not a new crash-durable recovery protocol.

```sh
python3 scripts/run-benchmarks.py --build-dir build --mode timing \
  --profile extended --filter 'export_*' --sizes-mib 1 256 \
  --repetitions 3 --output dist/benchmarks/export-after.json
```

`export_memory_alac`, `export_memory_wav`, `export_owned_alac` and
`export_owned_wav` time ordinary 16-bit export, including output close and
replacement. Input import and validation are outside timing. Each exported sample
is checked by streaming the output back, allowing one 16-bit quantization step
because ordinary export quantizes the fixture's normalized floats. Codec tests
separately check exact packet round trips. Disk cases exercise the session reader
binding with a 2 MiB decoded cache. Process peak RSS includes source import and
validation, so resident-input cases retain the whole source and disk-input cases
also include import indexes and summaries. These measurements exclude the save
job's subsequent persistent-waveform-cache rebuild and do not measure GUI latency.
Default opening/editing/playback/recovery still use the legacy document backend;
streaming export alone does not activate disk-backed editing.

Queued playback now pins a `PreparedPlayback` on the control side. Resident
documents retain their direct sample-access path. A session with a disk read
revision supplies that reader to the same playback action, using `ReadAhead` to
fill eight preallocated 4,096-frame stereo slots (256 KiB of sample storage, plus
16 KiB of worker scratch). The worker prioritizes the current position and loop
entrance, then sequential lead. The callback pins immutable slots using lock-free
atomics; cache misses never call the reader, allocate buffers, lock a document or
wait for I/O. A shortage emits silence without skipping source audio. Initial
buffering is excluded from `playbackUnderrunFrames`; subsequent shortages are
counted in the device snapshot and logged once per playback. Read/preparation
failure is reported through the main-thread playback error path.

Playback messages carry borrowed prepared handles. The control side retains
ownership until callback retirement; final buffer/processor destruction runs on
the background release queue. At most eight prepared starts, active sources or
retired-but-still-reading sources are retained. A full queue rejects the start
explicitly. Stop and source replacement only mark retirement in the callback;
closing the I/O worker never joins it. Existing PortAudio stream shutdown still
stops/closes the hardware stream. Legacy immediate-message test entry points are
not the normal prepared playback path. Recording and application-wide scheduling,
memory admission and I/O prioritization remain separate migration work.

```sh
python3 scripts/run-benchmarks.py --build-dir build --mode timing \
  --profile extended --filter 'playback_*' --sizes-mib 1 256 \
  --repetitions 3 --output dist/benchmarks/playback-read-ahead.json
```

`playback_memory` and `playback_owned` use the production device command queue
and callback, without opening a hardware stream or GUI. Source loading/import is
setup. Eight ranges across the document measure start submission, first available
samples (with 1 ms polling), 256-frame warm callbacks across loop boundaries, and
stop. Every warm-loop sample is checked outside timing, and warm-loop underruns
fail validation. A separate 128-callback sequential pass is paced at 48 kHz to
exercise read-ahead replenishment. It reports callback p99 and underrun frames,
validating both delivered samples and controlled silence on a miss. The main
iteration timer sums warm callback execution only; paced waiting, startup,
validation and cleanup are outside it. Disk cases use a 2 MiB decoded cache.
This is a headless callback/replenishment benchmark, not a hardware underrun,
cold-storage, scheduler contention or GUI latency measurement. Existing audio
tests additionally cover selection updates and effect preview; focused read-ahead
tests cover blocked reads, source release, edited disk ranges and partial EOF.

Playback, export, structural commands and effect jobs consume the disk revisions
now produced by normal queued opening. See `src/main/storage/README.md` for the
remaining compatibility paths, scheduling and memory-accounting work.

`edit_command_memory` and `edit_command_owned` time the actual delete command,
undo and redo for one frame near the beginning. Import/initialization is setup.
The owned case uses 1,024 preexisting edits to exercise a fragmented index and
128 command cycles per child; the resident case uses one cycle because it shifts
and processes the recording. `production_commands` reports each operation's
median, p99 and maximum; p99 for a single resident cycle is just that observation.
The iteration timer is the mean delete + undo + redo time. A final undo per
cycle is outside timing. Owned commands must perform zero sample-file I/O.
All restored samples are validated after timing. This measures command/commit
latency, not subsequent asynchronous waveform delivery or GUI event latency.

`effect_fixed_{memory,owned}` and `effect_all_{memory,owned}` submit the real gain
job and publish its result through the production history path. Both multiply
by 0.5, respectively across 1,000 frames and the whole document. Submission,
publication and total completion are reported separately; decoding/import and
initial peaks are setup. Ordinary disk undo storage is attached for the resident
case, while owned history retains references. Autosave and GUI presentation are
excluded. Owned effects generate source peaks during output; subsequent viewport
preparation is excluded. Every output sample is checked outside timing. Process
peak RSS includes setup and validation and is not a managed-allocation budget.

`sample_command_memory` and `sample_command_owned` use the same setup as the
structural command cases, replacing one sample through `SetSampleValue` and
undoing/redoing it. Owned cases assert zero sample-file I/O and start with 1,024
edits. The resident implementation already supports inexpensive point edits;
these cases expose the constant overhead of immutable root changes rather than
assuming every operation improves. GUI drag-event delivery is outside timing.

`normalize_{legacy,memory,owned}` analyzes both channels of an almost whole-file
selection with unaligned ends. The legacy case runs the previous synchronous
base-level peak scan (raw samples when peaks are dirty); memory and owned cases
submit the production `PeakAnalysis` worker,
which uses summaries with exact boundary samples. Submission and completion are
separate; completion includes worker startup and result polling. Initial audio
and summaries are setup. Every result must equal the fixture's exact peak.

Operation-only command and resident effect cases explicitly clear `State::paths`
to disable autosave. Owned effects use `BenchPaths` rooted in the runner's temporary
directory when preparing their worker, then clear paths before publication to
exclude autosave. Default-constructed
`State` has live application paths and must not be used unmodified in benchmarks.
Earlier command/effect reports from before this isolation fix inadvertently
included resident autosave work and must be regenerated. The runner removes each
child's working directory, including effects retained when the forked child exits.

`save_worker_{generic,preserving}_{memory,owned}` imports a PCM16 WAV fixture (sizes describe decoded float data),
changes one sample near the beginning, and runs the production background save
worker. Submission and completion are separate. Completion is observed with
100-microsecond polling, so sub-millisecond differences include polling and
scheduling noise. Both backends omit post-save waveform-cache persistence and UI
publication; those have separate correctness checks. The application’s resident
save still performs its post-save cache work, while revision saves defer peak
persistence. All saved samples are validated through a streaming decoder outside
timing: exact PCM16 values for preservation, the existing one-LSB tolerance for
ordinary export quantization, plus exact frame count and encoding. Owned
preserving cases assert zero decoded-sample store reads; original
PCM source-byte reads and output writes are still required. All working output
is under the runner’s temporary directory; `State::paths` is explicitly disabled.

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

`record_fixed_owned` records 65,536 stereo frames into an imported document;
`record_long_owned` records as many frames as the document contains. Both start
17 frames into the document, exercising overwrite and (for the long case)
extension. They use the production recording worker, publication coordinator and
reference history. Fixtures/import and sample validation are outside the timer.
The producer sends at most 64 callback-sized chunks per handoff and waits for
queue space between handoffs; completion measures unrestricted throughput, not
wall-clock microphone recording time. `max_handoff_ms` includes fixture sample
generation; `max_publication_ms` includes final history insertion. Both exclude
SDL painting. Original-sample reads must be zero, output is compared sample by
sample, and undo/redo must restore the exact retained roots. The benchmark does
not claim a resident-path speedup or measure audio-device contention. Peak RSS
includes import, validation and retained peak/index metadata.

`checkpoint_initial_owned`, `checkpoint_edit_owned`, `checkpoint_history_owned`
and `recovery_owned` measure durable revision persistence. Import is setup.
Initial checkpoint includes ownership of samples/original bytes and all indexes
and peaks. Edit checkpoint starts from a persisted document, changes one sample,
then measures reference capture and incremental writing. The history case starts
with 1,000 point edits and measures the 1,001st checkpoint. Normal recovery loads
the persisted document and installs matching history without reading samples.
These cases use the production persistence service, not UI autosave polling.

```sh
python3 scripts/run-benchmarks.py --build-dir build --mode timing \
  --profile extended --filter 'checkpoint_*' --sizes-mib 1 256 \
  --repetitions 3 --output dist/benchmarks/revision-checkpoints.json
python3 scripts/run-benchmarks.py --build-dir build --mode timing \
  --profile extended --filter recovery_owned --sizes-mib 1 256 \
  --repetitions 3 --output dist/benchmarks/revision-recovery.json
```

`revision_persistence` reports capture/completion time, appended node count,
logical copied sample/original bytes and metadata bytes written. Filesystem
clones count their logical length; this is not physical disk traffic. Incremental
point checkpoints must copy no sample/original payload. Normal recovery must
perform no sample reads before validation. Every recovered sample and applicable
undo/redo root identity is checked outside timing. Histories still require full
manifest metadata serialization; initial ownership and resident recovered
indexes/peaks scale with source length. OS caching is uncontrolled, RSS includes
setup/validation, and these cases do not establish power-loss durability, GUI
event latency or application-wide bounded memory.

Normal opening cases now exercise the activated disk backend. The timers still
cover queue submission through background completion, including peak persistence.
Validation reads the session range reader in bounded blocks; timers and work
counters stop before validation. `peak_process_rss_bytes_before_validation`
records lifetime peak RSS at that boundary for revision sessions. The existing
`peak_process_rss_bytes_including_setup` still includes validation: reading every
sample may fill the shared cache (10% of physical RAM), so it must not be mistaken
for memory consumed just by opening. Older reports lack the earlier RSS field;
the two measurements are not interchangeable. Core event probes exclude painting.

Owned save-worker cases now include retaining an independent output container,
without a second decode. On macOS, cloning normally avoids another full physical
copy; the bounded copy fallback requires output-sized I/O. The preserved edit root
continues to own its original sample sources. Consequently, save completion and
logical disk storage can increase even though decoded memory remains bounded.

`bulk_busy_edit_owned` (extended profile) occupies both shared bulk workers,
queues an effect, switches tabs and performs 128 point edits. It reports
`coordination.submission_ms`, `edit_p99_ms`, and `bulk_running`, then validates
both histories and the edited samples. The measured operation excludes import
and waiting for the queued effect. Compare small and large fixtures to detect
duration-dependent command costs. It does not measure SDL event latency.

## Imported large-file workflow

`large_file_workflow` exercises queued production import, progressive publication,
local edit/history commands, revision marker splitting, the session's asynchronous
viewport worker, queued float WAV save and reopen. Full sample/marker validation
runs after timing and import-memory capture. The sample cache is shared across
imported tabs and capped at 64 MiB. Local command timings exclude autosave and
clipboard persistence, as in command microbenchmarks; save/reopen uses normal paths.
The benchmark fails for source-sample I/O during local edits, excess sample-cache
residency, stale/pending viewport results, incorrect audio, warm viewport p99
at or above 16.7 ms, or event latency at or above 50 ms (p99 with at least 1,000
probes, otherwise the observed maximum). Process RSS is reported separately.

Use the existing native Release build, incrementally:

```sh
cmake --build build --target cupuacu-benchmarks -j8
python3 scripts/run-benchmarks.py --profile extended --mode timing \
  --filter large_file_workflow --sizes-mib 1 256 --repetitions 3 \
  --output dist/benchmarks/workflow-small-large.json
python3 scripts/run-benchmarks.py --profile large --mode timing \
  --filter large_file_workflow --sizes-mib 2048 --repetitions 1 \
  --formats wav m4a --max-rss-mib 2048 --max-disk-mib 32768 \
  --timeout-seconds 180 --output dist/benchmarks/workflow-2g.json
python3 scripts/run-benchmarks.py --profile large --mode timing \
  --filter large_file_workflow --sizes-mib 2048 --repetitions 1 \
  --formats wav --workflow-tabs 4 --max-rss-mib 2048 --max-disk-mib 32768 \
  --timeout-seconds 180 --output dist/benchmarks/workflow-four-tabs-2g.json
```

Sizes refer to decoded float audio. `workflow-tabs` participates in case identity,
so one-tab and four-tab measurements cannot be compared accidentally. ALAC fixture
generation streams an `AudioReader` rather than allocating the complete fixture.
Results distinguish metadata, first playable samples, first waveform, editable
state, peak persistence, save and reopen. `workflow_slow_pumps` records application
iterations exceeding 10 ms. See [the milestone report](../../PERFORMANCE-MILESTONE.md)
for measured results, the retained latency outlier, and scope limits.

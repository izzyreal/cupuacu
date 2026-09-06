# Imported large-file performance milestone

Delivered 2026-09-06. This closes the narrowed imported-document milestone,
not all five stages of the original architecture. Ready for manual verification.

- [x] Revision marker splitting shares audio slices and preserves markers.
- [x] Import metadata precedes persistent-peak lookup.
- [x] Editing becomes available independently of peak-file persistence.
- [x] Production workflow benchmark with a shared injectable sample cache.
- [x] 1/256 MiB WAV/ALAC/FLAC, three repetitions each.
- [x] 2 GiB WAV/ALAC and four simultaneous 2 GiB documents.
- [x] Progressive availability, bounded sample cache, zero source-sample I/O
      for local edits/history, asynchronous viewport and latency checks.
- [x] Cancel/close, retained clipboard, undo/redo, save/reopen and failure checks.
- [x] One final native headless suite and native app build.

## Behavior delivered

Imported audio uses the existing immutable disk-backed revision path. A shared
application cache defaults to 10% of physical RAM; the workflow benchmark injects
64 MiB across all imported tabs. Published sealed blocks support browsing and
playback before decoding finishes. Editing waits for audio import, not peak-file
writing. Immutable peak snapshots use the existing bounded writer queue, retrying
full admission without blocking edits or publishing subsequently edited peaks
under the original source key. Metadata is published before peak-file lookup.

Split by markers now creates reference slices for revision-backed documents,
including unaligned boundaries, without reading or copying source samples.

The scheduler changes included in this checkpoint run import/effect/save/autosave
jobs on two shared bulk workers, with bounded admission and effect scratch
reservations. Document operation identity controls publication, cancellation and
mutation availability; unrelated tabs remain usable. Completed/closed job objects
are released away from the UI thread. This is partial scheduling consolidation,
not a claim that every worker and resource uses one scheduler.

## Measurements

Native Release, macOS arm64, AppleClang 21, eight CPU cores, 8 GiB RAM, local SSD.
File sizes below mean **decoded float audio**, not compressed file size. OS file
cache is uncontrolled. Event probes use the production application loop without
GUI painting. Viewports use the session's asynchronous production worker.

| Workload | First playable | Editable | Event p99 | Warm viewport p99 | Import peak RSS |
|---|---:|---:|---:|---:|---:|
| 2 GiB WAV | 6.91 ms | 4.09 s | 1.61 ms | 0.253 ms | 135 MiB |
| 2 GiB ALAC | 14.76 ms | 12.46 s | 1.58 ms | 0.265 ms | 140 MiB |
| Four 2 GiB WAV tabs | — | 15.87 s, all tabs | 1.60 ms | 0.263 ms | 260 MiB |

Each large case had over 2,000 event observations (7,937 for four tabs), exceeding
the 1,000-observation minimum for p99. Final large runs met the 50 ms event and
16.7 ms warm viewport targets. These are measured observations, not guarantees
for other hardware or concurrent system activity.

All 18 routine workflow cases passed. Their warm viewport p99 ranged from
0.244 to 0.433 ms. Local point/delete/cut/paste/split/undo/redo checks performed
zero source-sample I/O. Single-document point edits measured below 0.13 ms;
the final four-tab point edit measured 4.03 ms. The shared decoded cache never
exceeded 64 MiB, including full validation reads after the measured workflow.

Import RSS is captured before validation fills the sample cache. RSS includes
peaks, indexes, libraries and scratch; it is **not** bounded by the sample-cache
budget. Other managed memory is not comprehensively accounted. Full sample
validation streams bounded buffers after timing. Float WAV save/reopen is checked
exactly; integer preservation saves retain their separate correctness coverage.
Local edit timings disable autosave/clipboard persistence, matching the existing
command microbenchmarks; they measure publication rather than durable completion.
Normal persistence paths are restored for queued save/reopen.

### Retained latency failure

The first four-tab 256 MiB run recorded event p99 **119 ms**, maximum **155 ms**.
Its functional checks passed but it failed the latency target. A single
instrumented confirmation recorded maximum 2.43 ms and no application iteration
above 10 ms; it had insufficient observations for p99. The longer four-tab 2 GiB
run subsequently passed (maximum event delay 27.02 ms). No code fix is claimed
for that unexplained outlier. Raw results are retained; watch this during manual
verification. The benchmark now fails automatically when its latency gate fails.

### Matched import comparison

Baseline executable: `/tmp/cupuacu-workflow-before/cupuacu-benchmarks`, retained
before this milestone's split/cache-publication changes (already containing the
scheduler slice). Before/after used identical Release flags and three repetitions
at 1/256 MiB, WAV/ALAC/FLAC, cached/uncached: 72 successful measurements.
No completion regression exceeded **both 10% and 5 ms**. Notable medians:

| Uncached 256 MiB | Completion before → after | Editable before → after |
|---|---:|---:|
| WAV | 468.98 → 495.15 ms | 468.98 → 465.68 ms |
| ALAC | 1571.17 → 1517.49 ms | 1571.17 → 1489.11 ms |
| FLAC | 1076.40 → 1075.01 ms | 1076.40 → 1045.52 ms |

The WAV completion increase is 5.6%/26.17 ms; editing becomes available before
persistence finishes. Small ALAC completion increased 11%/1.19 ms. Neither meets
both investigation thresholds. Do not infer a general decoder speedup from these
measurements; this change primarily separates availability from persistence.

## Validation and reproduction

Final native suite: **625 cases, 2,213,956 assertions passed**. Reporting tests:
**16 passed**. Native `Cupuacu` app build passed. No Linux builds or automated GUI
integration runs were used. Focused tests deliberately stall the decoder and
saturate/fail the peak writer while testing readable audio and editable documents.
Existing revision/viewport tests cover exact boundary peaks, summary reuse, stale
results, cancellation, save failure, histories and clipboard retention.

See [benchmark instructions](src/benchmark/README.md) for the workflow commands.
Raw reports remain in ignored `dist/benchmarks/`:

- `workflow-small-large.json`: 18 routine workflow runs.
- `workflow-2g.json`: WAV and ALAC, 2 GiB each.
- `workflow-four-tabs.json`: initial 256 MiB/tab latency failure.
- `workflow-four-tabs-diagnose.json`: instrumented confirmation.
- `workflow-four-tabs-2g.json`: final four-tab stress run.
- `workflow-open-before.json`, `workflow-open-after.json`: matched imports.

## Explicitly deferred

Paged peaks/indexes, a total application memory budget and pressure management,
and further scheduler unification remain outstanding. Legacy recovery now migrates
common snapshots/history; shape-changing recording histories retain compatibility
recovery as described below.
Memory mapping is outside the current scope.
The delivered imported workflow remains available for manual verification while
the explicitly authorized resource work continues.

## Follow-up: decoded reopening and progress presentation

The progress panel's previous centered placement, dimensions and typography are
restored while retaining nonmodal document operations. Progress identifies the
original filename and the preparation phase, not the internal working-copy path.

Completed imports can now be reused from `state/decoded-cache`, including after
restart. The optional cache has an 8 GiB reusable-data budget, skips active readers
during eviction, and never changes clipboard/history ownership. Original source
changes invalidate reuse. Cache writing is asynchronous and admits one maintenance
job at a time; insufficient space, corruption or failed writes fall back to normal
import. See the storage README for cache identity, leasing and admission details.

Measured on the same native Release machine (sizes are decoded float audio):

| Workload | Initial editable | Persisted-cache reopen |
|---|---:|---:|
| 256 MiB WAV, median of three | 519 ms | 46.2 ms |
| 256 MiB ALAC, median of three | 1.48 s | 43.6 ms |
| 256 MiB FLAC, median of three | 1.04 s | 43.6 ms |
| 2 GiB WAV | 3.98 s | 342 ms |
| 2 GiB ALAC | 12.39 s | 423 ms |

All 18 small/large reopen runs passed. The 2 GiB runs passed full sample validation
and wrote zero new decoded sample bytes on reopening. Their reusable disk entries
occupied approximately 3.06 GiB (WAV) and 2.89 GiB (ALAC), including original bytes,
float samples and peaks. First-import cache persistence completed approximately
301/365 ms after editing became available. Filesystems without cloning may incur
substantially more background copying; these timings are APFS observations.

The first 2 GiB ALAC reopen recorded a 53.24 ms maximum event delay; a single
confirmation reopened in 347 ms with a 4.43 ms maximum. Neither had enough events
for p99. No fix is attributed to that non-reproduced spike. A subsequent code review
separately removed cache-initialization lock contention on live hits and limited
cache-fill admission, keeping one bulk slot available for user work.

Reports: `reopen-decoded-small-large.json`, `reopen-decoded-2g.json`,
`reopen-decoded-2g-confirmation.json` and `reopen-workflow-check.json`, under
`dist/benchmarks/`. Native tests cover persistence, exact original PCM32 bytes,
markers, bounded sample residency, eviction/pinning, competing cache ownership,
source invalidation, truncated-cache fallback and bounded queued cache creation.

Final follow-up checks: **630 native test cases / 2,739,415 assertions** and
**16 reporting tests** passed; the native app build passed. The 256 MiB ALAC
production workflow retained zero source-sample I/O for local edits and a
0.246 ms warm viewport p99. Cached imports receive fresh runtime provenance IDs,
with a test verifying distinct reopened-document identities and matching sample
provenance, while retaining exact original container bytes.


## Follow-up: restored clipboard paste into Untitled

A fresh tab had zero channels and no read revision. Pasting a persisted revision
clipboard therefore converted the entire selection to resident samples and
per-sample metadata, then entered legacy paste/undo work on the main thread.
Legacy paste used the target's zero-channel format for its replacement, leaving
an empty document after that work.

A history-free empty target now establishes an empty saved revision before
pasting. An unconfigured tab adopts the clipboard format; a configured empty
document retains its format. Paste shares source references and summaries, undo
returns to an empty saved revision, and redo retains the inserted audio after
clipboard replacement. The waveform children are refreshed when the tab gains
channels. Existing resident documents with history still use the existing
conversion path; this fix does not complete general legacy-document migration.

The user's persisted clipboard (176,530,667 frames) passed full sample validation:
restoration 619 ms, paste 0.135 ms, undo 0.000792 ms, redo 0.001209 ms. Paste and
history performed zero sample I/O; process peak RSS including archive loading
and validation was 143 MiB. The original archive was read without modification.
These are headless command measurements, not measured GUI frame latency.

The new `paste_restored_empty` benchmark passed all nine runs (three repetitions
per size), copying the middle half of each source before persistence/restoration:

| Source decoded size | Clipboard decoded size | Median paste | Median restore |
| --- | --- | --- | --- |
| 1 MiB | 0.5 MiB | 0.0458 ms | 0.851 ms |
| 256 MiB | 128 MiB | 0.0493 ms | 40.6 ms |
| 2 GiB | 1 GiB | 0.0624 ms | 325 ms |

All runs required zero sample I/O for paste/undo/redo and validated every pasted
sample after timing. Archive restoration still scales with stored metadata;
this change removes clipboard materialization from the paste operation.
Report: `dist/benchmarks/paste-restored-empty.json` (ignored raw artifact).

Validation: 23 focused native cases / 721,955 assertions, including persisted
clipboard boundaries, configured empty targets, clipboard replacement, dirty
state and recovery of the pasted document's undo history; 16 reporting tests.
Native application and benchmark builds passed. No full suite, Linux or GUI
integration tests were run for this fix.


## Follow-up: new documents use revisions

The New File command now binds an empty saved revision in the selected channel
count, sample rate and format. Existing revision editing, waveform, recording,
export and recovery paths therefore apply from the first mutation. Recording
uses the existing bounded disk writer and incremental peaks, with one history
entry that undoes to the empty saved document. This closes new-document backend
migration; unconfigured startup tabs still acquire a format on New File or their
first revision clipboard paste. Previously persisted resident documents retain
the legacy recovery/conversion path.

Matched native Release measurements, three repetitions per case:

| New-document operation | Before | After |
| --- | --- | --- |
| Insert 1 MiB silence | 9.289 ms | 0.0160 ms |
| Insert 16 MiB silence | 127.308 ms | 0.0158 ms |
| Insert 2 GiB silence | Not run | 0.0128 ms |
| Point edit in 16 MiB document | 0.017 ms | 0.0030 ms |

Silence uses symbolic revision leaves, so those timings do not imply comparable
throughput for writing real samples. All nine after-change editing runs passed
complete sample validation. The 1/16 MiB comparisons used the same benchmark
commands and build flags; no measured command regressed.

`record_new` passed six runs at 1/256 MiB (three repetitions each). Median storage
completion was 2.57/545 ms; exactly the recorded sample bytes were written. The
largest handoff across these runs was 0.171 ms and the largest publication was
0.039 ms. Queue occupancy stayed at or below 320 of 512 slots. This is accelerated
headless storage throughput, not a physical-device or GUI-latency measurement.
Samples and recording undo/redo were validated after timing.

Focused native checks covered tabs, revision commands, clipboard paste,
recording, save and recovery. Recording now starts with the production New File
command in FLOAT32 and PCM16, consumes callback chunks through the production
drain, restores its history after recovery, and saves/reopens its samples. The
PCM16 save check allows one quantization step; an initially strict less-than
comparison was corrected to include exactly one step. All eight recording cases
passed after that test correction; the other 45 focused cases had already passed.
The 16 reporting tests and native application/benchmark builds passed. No full
suite, Linux or GUI integration run was added.

Reports in `dist/benchmarks/`: `new-document-before.json`,
`new-document-after.json`, `record-new.json`.


## Follow-up: large M4A durations and offsets

M4A ALAC/AAC parsing, file information, decoding totals/progress and import frame
counters now carry 64-bit durations. ALAC encoding results and export accept
recordings beyond 2^32 frames while retaining bounded packet buffers. Movie,
track and media durations, plus chapter edit lists, use version-1 wide fields
when needed. Track-ID parsing recognizes both header layouts. Large media data
and chunk offsets continue through the existing extended `mdat`/`co64` path.
The conservative wide-duration threshold matches
[FFmpeg's writer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/movenc.c).

Timing-table expansion is checked against the packet table before allocation,
frame totals use checked arithmetic, and resident compatibility decoders check
byte-size multiplication before reserving output. Export validates duration and
chapter constraints before starting packet work. Cancellation still leaves an
existing destination unchanged.

Container constraints remain explicit: packet counts use 32-bit sample-table
fields, metadata atoms use the existing 32-bit size representation, and an
individual chapter duration cannot exceed UINT32_MAX sample ticks. The latter
is the format's [time-to-sample field width](https://developer.apple.com/documentation/quicktime-file-format/time-to-sample_atom/time-to-sample_table),
not the total track-duration limit. Wide chapter positions and total chapter
track duration are supported. No chapter positions are silently truncated.

Boundary tests use sparse files and synthetic timing tables: >2^32 ALAC/AAC
frames, a real decoded ALAC packet beyond a 4 GiB prefix, chapters beyond 2^32
frames and 4 GiB offsets, wide decode progress after a real first packet,
preflight rejection, bounded export reads and transactional cancellation.
The long-duration fixtures are intentionally not fully encoded/decoded; existing
short-file codec/round-trip tests and ordinary export benchmarks cover actual
sample processing. This closes the application-level M4A frame-limit slice,
not unbounded codec index memory or support for every MP4 container variation.

`m4a_metadata` passed six runs (three small, three beyond the frame boundary).
The wide case describes 4,295,098,368 frames and 1,048,608 packets in a sparse
5,247,235,524-byte file, with 4,195,524 non-audio bytes. Median parsing was 7.04 ms;
peak RSS including setup was about 35.3 MiB. The small case parsed in 0.081 ms.
No complete media payload is generated or scanned by this metadata benchmark.

Six ordinary ALAC export runs (1/256 MiB, three repetitions) passed sample
validation. Medians: 10.81 ms / 2512.37 ms. The historical `export-after.json`
report recorded 9.72 ms / 2516.98 ms. The small difference is +1.10 ms / 11.3%;
the large case is essentially unchanged. This is a historical reference, not a
matched same-session regression attribution. The small difference is below the
previously agreed combined investigation threshold of 10% and 5 ms.

Reports: `m4a-small-metadata.json`, `m4a-wide-metadata.json`,
`m4a-wide-export.json` in `dist/benchmarks/`.

Validation completed: 57 distinct focused native codec/export cases passed across
the initial suite and added AAC boundary check; the final five boundary cases
passed again after the duration-header compatibility adjustment. Six metadata
and six actual-export benchmark runs passed; 16 reporting tests passed. Native
application and benchmark builds passed. No full suite, Linux or GUI integration
runs were added for this slice.


## Follow-up: legacy recovery migration

Version-2 document and clipboard snapshots now stream into owned revisions,
generating peaks from the same bounded audio batches. Startup also converts
legacy segment versions 1–3, sample matrices and sample cubes referenced by
cut/delete/paste/trim, silence, effects, point edits, copy and recording history.
Commands retain before/after roots, editor state and clipboard references.
Marker history and trim views retain their existing behavior. Snapshot strings,
counts, offsets and sample-byte arithmetic are checked before allocation/reads.

Recovery publishes a revision archive atomically after conversion, so the next
load uses the archive. Cancellation and archive-write failure leave the original
snapshot and destination session unchanged. Recovery checks cancellation during
archive writes as well as sample conversion. The existing restart-history size
policy still applies. Legacy payload samples are preserved exactly as float32;
those old formats do not contain original higher-precision container bytes.

Histories containing a recording that changes format/channel count, including
undo to an unconfigured tab, retain the resident compatibility reader and original
commands. This preserves their undo behavior rather than silently changing the
old document shape. Unsupported or damaged histories retain a copy of the
snapshot and undo directory in `legacy-recovery-retained`, restore intact current
audio, and use the existing history-warning reporting. Retained backups include
original path information and are not automatically pruned. Successfully migrated
legacy undo stores remain attached for normal tab-close cleanup.

This is not removal of every resident compatibility path. Peak/index residency,
the total managed memory budget, and additional scheduling work remain separate.

Native Release measurements, three repetitions per size (decoded float bytes):

| Size | Former resident load | One-time migration + archive | Archive reload |
| --- | --- | --- | --- |
| 1 MiB | 0.69 ms | 5.20 ms | 0.94 ms |
| 16 MiB | 6.11 ms | 39.17 ms | 3.29 ms |
| 256 MiB | 99.77 ms | 508.58 ms | 40.33 ms |
| 2 GiB | Not run | 4.59 s | 321.81 ms |

The former loader was compiled from `3dd8d30` with the same native Release flags
and the new scenario. Both generated the same version-2 snapshot. Its dirty/missing
peaks were left unbuilt by the former loader; migration builds them and creates an
archive. This is a comparison of production loader return behavior, not equal
completion work or a decoder-speed comparison. Archive reload is in the same
process, with filesystem caching uncontrolled; it is not full restart latency.

First migration has a real disk/persistence cost: +4.51 ms at 1 MiB, +33.06 ms at
16 MiB and +408.80 ms at 256 MiB. The larger first-load costs exceed the combined
regression-investigation threshold and are intentional one-time migration work.
Subsequent 256 MiB archive loading is about 2.5 times faster than the old resident
load. The 1 MiB archive load is about 0.25 ms slower. The initial 2 GiB conversion
used about 71 MiB peak RSS; the full run, including a second revision load and
validation, reached about 138 MiB. Former resident load at 256 MiB reached about
260 MiB, versus about 22 MiB for the new full benchmark at that size.

All 12 final benchmark runs passed sample validation. Deletion and undo stayed
below 0.02 ms in the size medians and performed zero sample I/O. The working store
wrote each sample once; archive persistence additionally clones or copies that
store. These APFS results do not predict fallback-copy throughput on other disks.

Validation: 51 focused native cases passed, followed by an additional case covering
all three legacy segment versions across channel and 65,536-frame boundaries.
The checks include all 14 migrated command kinds in undo and redo positions,
a second archive recovery, clipboard lifetime, legacy recording compatibility,
source retention, cancellation, and failed archive publication/retry. All 16
reporting tests and native application/benchmark builds passed. No broad full
suite, Linux or GUI integration runs were added.

Reports: `recovery-legacy-before.json`, `recovery-legacy-durable.json` under
`dist/benchmarks/`. `recovery-legacy.json` records the intermediate conversion-only
implementation and must not be used as the final durable-migration result.


## Follow-up: shared decoded-sample memory and scratch admission

Production imports, archives/recovery, effects, recording and clipboard conversion
now use the same decoded block cache. Its default ceiling is 10% of physical RAM,
with a startup override in `config/performance.json` (`audio_memory_mib`; zero
means automatic). Previously only imports shared the RAM-derived cache; other
contexts could accumulate separate allowances. Declared scheduler scratch now
reduces sample cache capacity before the job executes, including across independent
schedulers. Existing scheduler execution and queue limits remain in place.

macOS normal/warning/critical pressure notifications asynchronously restore,
halve or quarter the cache target. SDL low-memory events request critical trimming.
Running scratch reservations and reads drain normally; they cannot be reclaimed
to immediately satisfy a lower pressure target. Scratch admission uses the normal
configured ceiling. Native pressure monitoring on other desktop platforms remains
outstanding. No simulated system-wide pressure was applied during validation.

A disk miss no longer holds the shared cache mutex. In-flight sample arrays count
against the ceiling and release their accounting on success or failure. When all
capacity is reserved, reads use caller-owned buffers directly. This bounds decoded
cache arrays plus declared scratch, not total process RSS: peaks/indexes, metadata,
transport/caller buffers, codec/DSP internals, undeclared scratch and resident
compatibility paths still require their own accounting. Only existing effect job
scratch declarations are currently charged by production bulk scheduling.

Native Release benchmark, three repetitions per size, four stores sharing 8 MiB:

| Working samples | Peak cache + reserved scratch | Read every block | 10,000 warm reads |
| --- | --- | --- | --- |
| 1 MiB | 5 MiB | 0.41 ms | 0.154 ms |
| 16 MiB | 8 MiB | 4.10 ms | 0.219 ms |
| 256 MiB | 8 MiB | 54.80 ms | 0.219 ms |

The scan requests 31 samples per block, causing full-block cache fills. Filesystem
caching is uncontrolled; these are not physical cold-disk measurements. The scenario
also reserves 4 MiB of scratch (without allocating a simulated scratch payload),
checks displacement, requests critical trimming and releases the reservation.
All nine runs passed. Whole-process peak RSS was approximately 4/11/11 MiB;
reserved scratch is accounting capacity and need not appear in RSS.

Matched playback against the saved `1806198` executable, same native Release flags,
passed all six runs on each version, with zero sequential underrun frames. Median
first-data latency was 1.18 → 1.16 ms at 1 MiB and 1.17 → 1.17 ms at 256 MiB.
Sequential callback p99 medians were 0.0128 → 0.0155 ms and 0.0259 → 0.0164 ms.
The small-workload increase is 0.0027 ms; no material playback regression appeared
in these sampled workloads. This does not measure playback under system pressure.

Validation: 35 focused native cases passed (memory, scheduler, owned storage,
revision recording/persistence/effects), including five memory cases; 16 benchmark
reporting tests passed. Native app and benchmark builds passed. No full suite,
Linux build or GUI integration runs. Reports under `dist/benchmarks/`:
`shared-memory-budget.json`, `shared-memory-playback-before.json`,
`shared-memory-playback-after.json`.

Next milestone: page detailed peaks/indexes and integrate their residency with
resource admission. This slice does not close the original total-memory stage.


## Follow-up: disk-backed detailed revision peaks

Long-source revision summaries now retain levels of at most 4,096 peaks in RAM
and store detailed levels in application-owned temporary segment files. A stereo
source retains less than 128 KiB of overview values, excluding metadata. Detailed
pages use the decoded-sample cache, including its shared budget and pressure
trimming. Their page index is arithmetic plus a small descriptor per level;
there is no in-memory pointer for every detailed peak page.

Each 256 KiB page holds a spatial subtree: 16,384 base peaks and their next 13
summary levels. Grouping nearby levels matters: the first level-by-level layout
thrashed a 1 MiB cache, reading about 18 GiB for the 2 GiB-equivalent benchmark.
That layout was rejected. The final subtree layout reads about 130 MiB over the
two eight-view passes and avoids the measured 1.6-second stalls.

Imports, generated effects, clipboard conversion, legacy conversion and archive
recovery construct paged summaries on workers. Small sources keep their existing
resident representation. Recording batches already have small per-source summaries.
Revision overview queries are explicitly worker-only; legacy progressive rendering
retains its in-memory caches. Archive serialization reads peak ranges through the
new interface; the durable archive format is unchanged. Recovery recreates temporary
peak pages from the archive and still rebuilds corrupt summaries from audio.
Temporary segments live with their summaries and are removed on normal final release;
crash leftovers remain subject to operating-system temporary-file cleanup.

Native Release, three repetitions per case. The synthetic workload covers two
channels, eight 1,200-pixel views at different zooms, and a second identical pass.
Only 1 MiB is available for detailed pages:

| Audio represented | Resident peaks, old | Resident overview + maximum cache | First eight views, resident / paged | Repeated eight views, resident / paged |
| --- | --- | --- | --- | --- |
| 1 MiB | 32 KiB | 32 KiB + 0 | 0.46 / 0.35 ms | 0.45 / 0.33 ms |
| 256 MiB | 8 MiB | 128 KiB + 1 MiB | 0.51 / 2.96 ms | 0.51 / 2.58 ms |
| 2 GiB | 64 MiB | 128 KiB + 1 MiB | 1.53 / 15.47 ms | 0.64 / 15.03 ms |

The small case takes the same resident path in both variants; its timing difference
is not a paging improvement. Large queries are slower than fully resident queries.
This is a memory/responsiveness trade-off, not a claim of faster RAM access. Repeating
eight views exceeds the tiny cache, so the second pass still reads files. Filesystem
cache state is uncontrolled; these are not cold-device latency guarantees.

Building the upper pyramid and writing paged details took 12.34 ms / 98.91 ms at
256 MiB / 2 GiB equivalent, compared with 2.30 ms / 17.77 ms for the resident
pyramid. A matched production import benchmark against the saved pre-change
executable measured 2.53 → 2.83 ms at 1 MiB and 411.69 → 434.86 ms at 256 MiB.
First-waveform publication at 256 MiB was 2.09 → 2.05 ms. The import increase is
23.17 ms (5.6%), with no delayed first waveform in these runs.

The existing asynchronous raw-window benchmark also passed all samples/cache
bounds. At 256 MiB its median 64-request aggregate rose 8.80 → 31.51 ms;
per-request median completion rose 0.129 → 0.178 ms and the median run maximum
0.277 → 2.814 ms. One run's maximum was 5.761 ms. These are real measured costs
following import, with unchanged sample reads/cache hit counts; they are not
attributed conclusively to a specific system-I/O effect. Submission remained
about 0.001 ms. The individual worker costs remain small, but the aggregate
regression is recorded rather than dismissed.

All 18 synthetic runs and 24 matched import/raw-window runs passed. Validation:
33 focused native cases (paged boundaries/cancellation/archive round trip,
viewport pipeline, owned audio, revision persistence/effects and legacy recovery),
16 reporting checks, native app and benchmark builds. No broad suite, Linux build
or GUI integration runs. Later formatting-only edits did not change behavior.

Reports in `dist/benchmarks/`: `peak-paging.json`,
`peak-paging-open-before.json`, `peak-paging-open-after.json`,
`peak-paging-import-before.json`, `peak-paging-import-after.json`.
`peak-paging-initial-layout.json` documents the rejected layout.

Remaining: initial peak construction and archive decoding still materialize the
peak pyramid before paging; progressive caches and pending persistence snapshots
can retain it temporarily. This slice therefore reduces retained revision memory,
not import peak RSS. Overview values and metadata are not charged to the shared
budget, and many short sources can accumulate resident overviews. Source audio
block indexes, provenance runs and edit-tree nodes remain resident. Those need
bounded construction, aggregate admission and/or paging before the original
application-wide memory objective is complete. Memory mapping remains excluded.


## Follow-up: bounded peak construction and archive recovery

Recovery, generated effects and clipboard conversion now use
`StreamingPeakBuilder`. It reduces samples in bounded blocks, spools only base
summaries for long sources, and constructs spatial peak tiles without a full
resident pyramid. Sources with at most 4,096 base peaks keep a small memory
buffer. Arbitrary append boundaries and the final partial 128-frame bucket are
preserved. A failed read/write poisons the transaction; finish cannot publish
partially generated channels. Cancellation discards temporary working files.

`SourcePeaks::createStreaming` builds each tile and its summary levels from at
most 16,384 base peaks. Higher tile groups read already sealed lower summaries,
so there is no growing in-memory array of tile roots. The resulting page layout
and query behavior match the previous paging milestone. Per-source overviews
and level descriptors remain resident and outside aggregate admission.

Archive loading streams one base-peak record at a time and reconstructs derived
levels. It no longer loads the whole peak pyramid. The durable format is unchanged;
stored higher levels are no longer needed for loading. Invalid/truncated base
records trigger a bounded rebuild from audio. Source/index JSON and audio block
indexes still materialize in memory. Legacy histories that require the resident
shape-changing recording compatibility path still use its resident waveform cache.

Native Release synthetic benchmark, three repetitions per case, with a 1 MiB
page cache and two channels:

| Audio represented | Prior paged construction peak RSS | Streaming peak RSS | Prior / streaming preparation |
| --- | --- | --- | --- |
| 1 MiB | 2.67 MiB | 2.70 MiB | 0.027 / 0.045 ms |
| 256 MiB | 12.13 MiB | 4.28 MiB | 11.82 / 15.44 ms |
| 2 GiB | 68.80 MiB | 4.31 MiB | 94.67 / 77.10 ms |

The old preparation timer excludes base-array generation (which is included in
peak RSS); streaming generates its synthetic base values inside the timer.
These timings do not represent identical timed setup work or audio decoding.
All query results were validated. Eight-view times at 2 GiB remained approximately
15 ms for both paged variants. The 256 MiB streaming preparation increase is
3.62 ms; the small case increases 0.018 ms. This is bounded construction memory,
not a claim that total application memory is now bounded.

Matched production legacy-recovery results against the saved `094de56` executable:

| Size | Initial migration, before / after | Archive reopen, before / after | Initial peak RSS, before / after |
| --- | --- | --- | --- |
| 1 MiB | 4.71 / 4.62 ms | 0.77 / 0.40 ms | 4.59 / 4.61 MiB |
| 256 MiB | 509.76 / 518.12 ms | 49.07 / 28.03 ms | 21.36 / 13.14 MiB |

The 256 MiB first migration cost is +8.36 ms (1.6%); archive reopen is about 43%
faster. Initial peak RSS falls about 38%. These figures include archive publication
and its cache population, so memory savings differ from the isolated construction
benchmark. Filesystem caching is uncontrolled. A first implementation took 776 ms
for conversion; the benchmark exposed per-sample bucket-boundary overhead. It was
replaced with bucket-at-a-time reduction before accepting this slice. Leading NaNs,
signed zero and partial buckets retain the existing reducer's behavior.

Validation: 32 distinct focused native cases passed across targeted runs, covering
streamed/resident peak equivalence, bounded callback sizes, partial append and tile
boundaries, cancellation on the last input read, poisoned transactions, archive
round trips, revision effects/persistence, clipboard preservation metadata and
legacy history recovery. All 16 reporting checks passed. Native app and benchmark
builds passed. No full suite, Linux build or GUI integration runs.

Reports: `peak-streaming.json` (27 synthetic runs),
`peak-streaming-recovery-before.json` and `peak-streaming-recovery-after.json`
(six production recovery runs each), under `dist/benchmarks/`.
`peak-streaming-recovery-initial-loop.json` records the rejected reduction loop.

At that checkpoint, progressive import still built/retained its legacy UI and
persistence peak caches before paging; import peak-memory growth was unchanged. The bounded
builder is available for the next import migration, which must preserve incremental
drawing and persistent-cache reuse. Source audio indexes, provenance/edit metadata,
aggregate overview accounting and the wider scheduling/resource work also remain.


## Progressive import peak storage

Progressive imports now build a bounded online pyramid. The decoder and viewport
share published peak pages instead of retaining separate full-length arrays.
Completed spatial tiles spill into packed temporary segments through the shared
sample/peak cache. Import completion seals the tail and hands the same storage to
the immutable revision reader; there is no final full pyramid copy or repaging.
Active tile buffers grow with channel count and the logarithmic number of summary
groups, not with every peak. Finalized sources retain only their small top group.
These buffers/overviews are not yet charged to aggregate memory admission.

The existing progressive appearance is retained through worker viewport queries.
Only available regions are queried; missing regions stay pending, and the UI
never reads sample/peak files. Completed results invalidate their waveform texture.
Further availability requests wait for the current request to publish; navigation
still supersedes obsolete requests. Shared-reader notifications coalesce instead
of making the decoder wait for the UI to consume every decoded block.

Persistent v1 caches remain compatible. Loading reads base summaries in bounded
chunks, and asynchronous persistence streams the finalized reader. Cache preview
can cover the full waveform while audio is still decoding. Invalid/truncated cache
files fall back to peak generation during decode. No clipboard lifetime, opening
window layout, or source-ownership behavior changes are included.

Matched native Release WAV imports, three repetitions per size, no concurrent
compilation, filesystem caches uncontrolled:

| Decoded audio | Import completion before / after | Peak process RSS before / after |
| --- | --- | --- |
| 1 MiB | 2.88 / 2.70 ms | 4.55 / 4.77 MiB |
| 256 MiB | 452.69 / 440.54 ms | 14.22 / 5.95 MiB |

The large case reduces RSS about 58%, with no measured completion penalty. Small
RSS increases 0.22 MiB. Timings include owned import; full sample validation occurs
after timing and remains included in process RSS. The preserved baseline executable
was copied before this slice; its embedded build identity describes the preceding
uncommitted source fingerprint (the changes subsequently committed as `5c7abae`).

Synthetic progressive peak construction, three repetitions, 1 MiB shared cache:

| Audio represented | Peak RSS | Online preparation | Eight views / repeat |
| --- | --- | --- | --- |
| 1 MiB | 2.63 MiB | 0.019 ms | 0.48 / 0.48 ms |
| 256 MiB | 3.77 MiB | 11.42 ms | 3.95 / 3.57 ms |
| 2 GiB | 3.77 MiB | 86.94 ms | 17.01 / 16.75 ms |
| 8 GiB | 3.81 MiB | 377.59 ms | 59.96 / 58.98 ms |

Compared with the bounded offline streaming builder, online preparation costs
13.90 ms more at 2 GiB and 69.19 ms more at 8 GiB. It maintains queryable prefixes
throughout construction; the offline builder does not. Query overhead is below
0.25 ms per view in these measurements. Total query cost still rises with working
set and cache misses: eight views at 8 GiB read 256 MiB per pass with this deliberately
small cache. This is not a claim of disk-I/O independence from document length or
completion of the original viewport scaling goal. Every query is validated, and
the 8 GiB case crosses segment-file boundaries without a resident page index.

The 256 MiB production ALAC workflow passed: first waveform/playable data about
12.7 ms, editable in 1.50 s, warm viewport p99 0.257 ms, and zero source-sample I/O
for local edits/history. Its 752 event observations are insufficient for p99;
maximum observed event delay was 5.32 ms.

One 2 GiB ALAC workflow also passed: first waveform/playable data 14.1 ms,
editable in 12.14 s, event p99 3.22 ms across 6,068 observations, warm viewport
p99 0.257 ms, and import peak RSS 75.6 MiB with a 64 MiB shared cache. Local
edits/history again performed zero source-sample I/O. Save and reopen validation
passed. This is a single large workflow run, not a matched decoder-speed comparison.
These headless probes do not measure SDL painting or display presentation.

The first 2 GiB attempt hit the benchmark's 16 GiB logical-file quota during
save/reopen and was terminated by the runner. The successful rerun allowed 32 GiB;
this quota includes cloned files' logical sizes. Both reports are retained as
`progressive-peaks-alac-large-workflow.json` (quota failure) and
`progressive-peaks-alac-large-workflow-32g.json` (pass).

Validation: 27 focused native cases passed, including every summary level at
partial/exact tile boundaries, concurrent append/query/final sealing, arbitrary
sample append boundaries, partial viewport availability, import precision,
cancellation, persisted cache compatibility and corruption fallback. Native app
and benchmark builds passed. No full suite, Linux build or GUI integration run.

Reports in `dist/benchmarks/`: `progressive-peaks-before-idle.json`,
`progressive-peaks-after-idle.json`, `peak-progressive.json`,
`peak-progressive-streaming-reference.json`, and the ALAC workflow reports.
Earlier `progressive-peaks-before.json`/`after.json` timings overlapped compilation
and are superseded by the idle comparison.

Remaining resource work: page audio/source indexes and provenance metadata;
account for active peak tiles and aggregate overviews across many sources; finish
scheduler/resource admission consolidation and resident compatibility migrations.
Memory mapping remains excluded by agreement.


## Smaller waveform pages and byte-sized shared cache

Delivered after `ca157ec`. This addresses the measured peak-read amplification
left by the import migration, rather than claiming audio-index paging is complete.

Large sources now use 4 KiB spatial peak tiles instead of 256 KiB tiles. Each
contains 256 base values and neighboring summary levels. Small recordings with
at most 4,096 base values remain fully resident. Imports, archive loads, effects,
and clipboard conversion share the online hierarchy builder; streaming keeps
large sequential input reads and propagates parent summaries during append.
There is no second disk pass to construct higher groups. Durable formats and
waveform values are unchanged.

The shared cache allocates 4–256 KiB buffers according to block size. Resident
and in-flight allocations plus declared scratch obey the existing byte budget;
pressure evicts by bytes. Same-size buffers can be reused after eviction. The
minimum allocation bounds the number of entries, and hash lookup avoids the
extra tree-search cost when many small pages fit. Entry/bucket bookkeeping remains
outside buffer accounting, as do active peak tiles and aggregate overviews.

Native Release, three repetitions per synthetic case, 1 MiB shared cache. Sizes
mean stereo float audio represented; fixtures contain only synthetic peaks:

| Audio represented | First eight views, before / after | Repeated eight views, before / after | Peak reads per pass, before / after |
| --- | --- | --- | --- |
| 1 MiB | 0.480 / 0.545 ms | 0.477 / 0.514 ms | 0 / 0 |
| 256 MiB | 3.95 / 5.16 ms | 3.57 / 4.85 ms | 8.25 / 7.76 MiB |
| 2 GiB | 17.01 / 13.19 ms | 16.75 / 12.41 ms | 64.75 / 21.93 MiB |
| 8 GiB | 64.69 / 41.32 ms | 58.72 / 19.12 ms | 256 / 31.24 MiB |

At 8 GiB, repeated queries are about 3.1 times faster and peak reads fall 88%.
First-pass timing varies more: an earlier final-layout run measured 18.7 ms; the
standalone final run measured 41.3 ms. Both are retained. These are application
storage-byte counts and headless worker timings, with uncontrolled filesystem
caching, not physical cold-device or SDL presentation measurements. Peak RSS
remains below 4 MiB in the large synthetic cases. Cost still depends on viewport
width, summary traversal and misses; this does not close every original rendering
or resource-policy requirement.

Trade-offs remain explicit. The 256 MiB query increase is about 0.15 ms per view;
the 1 MiB increase is under 0.01 ms per view. Progressive 8 GiB peak construction
increases from 368.9 to 400.6 ms (+31.7 ms, 8.6%). Offline streamed construction
increases from the prior checkpoint's 308.4 to 356.9 ms at 8 GiB, while its repeated
eight-view time falls from 57.0 to 19.1 ms. These synthetic preparation timers do
not include audio decoding.

Matched production checks against `/tmp/cupuacu-before-peak-tiles/cupuacu-benchmarks`,
with the same Release flags and no concurrent compilation:

| Workload | Before / after |
| --- | --- |
| 1 MiB WAV import | 2.646 / 2.758 ms |
| 256 MiB WAV import | 417.80 / 425.45 ms (+1.8%) |
| 1 MiB legacy recovery | 4.837 / 4.695 ms |
| 256 MiB legacy recovery | 508.58 / 498.73 ms |
| 256 MiB durable recovery reopen | 27.45 / 28.41 ms |
| 256 MiB sample-cache scan | 55.55 / 58.29 ms |
| 10,000 warm reads after that scan | 0.220 / 0.132 ms |

All sample/peak validations and memory bounds passed. Import and recovery timings
exclude their subsequent full sample validation. Cache scan increases 2.74 ms
(4.9%); warm reads improve. The small absolute costs and larger-navigation gains
fit the agreed trade-off criterion; total application memory is still not bounded.

The benchmark caught two intermediate issues before acceptance: variable-size
entries made the tree lookup slower, addressed with hashing; smaller streaming
tiles caused a second summary read pass, addressed by sharing the online builder.
The intermediate reports remain available rather than being presented as final.

Validation: 41 distinct focused native cases passed across targeted runs, including
mixed-size cache admission, concurrency, pressure, failed-read release, small-tile
I/O and reuse, all summary levels, import/persistence compatibility, recovery and
playback read-ahead. After the final builder/arithmetic changes, the 20 affected
peak/recovery cases passed again. Native app and benchmark builds passed. No full
suite, Linux build or GUI integration run.

Final reports under `dist/benchmarks/`: `peak-pages-final-peak_progressive.json`,
`peak-pages-final-peak_streaming.json`, their `-large.json` counterparts,
`peak-pages-reference-large.json`, `peak-pages-final-open_owned.json`,
`peak-pages-final-recovery_legacy.json`, and the `peak-pages-*-before.json` /
`peak-pages-audio_memory-after.json` regression references. The smaller synthetic
baseline uses the preceding checkpoint's `peak-progressive.json` and
`peak-progressive-streaming-reference.json`. Intermediate reports are
`peak-small-tiles-progressive.json`, `peak-sized-cache-progressive.json`,
`peak-small-pages-final.json`, `peak-small-pages-streaming.json`, and
`peak-one-pass-streaming.json`.

Outstanding: audio/source index paging, provenance metadata scaling, aggregate
memory admission and remaining scheduler/compatibility consolidation.

## Follow-up: bounded source indexes and provenance

Source block directories, segment-length directories and explicit provenance
runs now use shared working indexes. Up to 256 records stay resident; larger
indexes spool to anonymous files and retain only a write tail and one read page.
Per-index record-buffer bounds are 12 KiB, 4 KiB and 20 KiB respectively. File
objects, allocator metadata and the number of live indexes are outside these
per-index bounds; application-wide admission remains subsequent work.

Progressive and completed import share the same index. Cache rebinding shares
the immutable directories and remaps source identity when reading metadata,
without cloning or rewriting the runs. Source archive records now stream
checksummed directory pages and identify each stream by offset/count. Peak
levels no longer retain a list of every persistent page ID. New manifests use
version 2; version 1 remains readable. Legacy monolithic records retain their
old parsing allocation until rewritten in the new format.

Dirty flags for hovered samples now arrive with asynchronous sample/viewport
results, so paging does not introduce status-bar disk I/O. Focused validation
also exposed combined clipboard/peak scratch exceeding the macOS worker stack;
clipboard metadata scratch is now a bounded heap allocation.

Splices coalesce adjacent compatible source ranges and identical constants,
including boundaries inside balanced trees. Repeated cut/paste restoration
therefore removes artificial index fragmentation. Distinct edit/history nodes
and archive identity maps are still resident. **The first plan area's full
bounded-history criterion is not closed.** Paging cold edit paths depends on
worker-owned edit preparation; that dependency must be handled with the remaining
memory/scheduling work, not silently treated as solved by coalescing.

Measurements: native Release, macOS arm64, three fresh child processes per case.
The resident-index reference and paged-index case generate identical 40-byte
records without sample audio. Their size labels scale record count, not physical
input files. Filesystem caches were uncontrolled.

| Metadata records | Resident-vector buffers | Paged buffers | Paged append / scan | 1,024 random lookups |
| --- | ---: | ---: | ---: | ---: |
| 16,384 | 640 KiB | 20 KiB | 0.84 / 0.26 ms | 1.18 ms |
| 262,144 | 10 MiB | 20 KiB | 8.54 / 3.61 ms | 1.10 ms |
| 4,194,304 | 160 MiB | 20 KiB | 131.43 / 61.19 ms | 1.83 ms |

The corresponding resident-vector append/scan times at 4,194,304 records are
31.10/6.08 ms and its random lookup batch is 0.032 ms. Paging trades additional
worker I/O and CPU for bounded residency; it is not a faster in-memory array.
Small indexes of at most 256 records do not open a working file.

The production archive scenario generates one provenance run per sample using
bounded scratch. At 4,194,304 runs it saves in 1,477 ms and restores in 1,806 ms,
retaining 22,016 bytes of combined source-index buffers; peak process RSS through
validation is 4.23 MiB. At 16,384 runs RSS is 3.91 MiB. Archive size grows to
56.27 MiB while transient working provenance uses approximately 160 MiB of disk.
These are adversarial metadata measurements, not representative ALAC import
times and not a baseline comparison for archive throughput.

Matched production checks, using the native executable saved before this edit
as the reference (its embedded git/build stamp predates the previous commit):

| Scenario | Before | After |
| --- | ---: | ---: |
| 1 MiB WAV import | 2.802 ms | 2.570 ms |
| 256 MiB WAV import | 429.753 ms | 447.099 ms |
| 1 MiB legacy recovery | 4.811 ms | 4.754 ms |
| 256 MiB legacy recovery | 522.668 ms | 523.254 ms |
| 1 MiB document delete | 0.00525 ms | 0.00200 ms |
| 256 MiB document delete | 0.00325 ms | 0.001917 ms |

Delete/undo/redo retain zero source-sample I/O. Undo/redo medians stay below
0.0003 ms. Import's 17.35 ms increase at 256 MiB (+4.0%) is retained as a measured
regression; the small case improves and recovery is essentially unchanged. No
claim is made that paging speeds up import. The initial reference run overlapped
compilation and was replaced by an uncontended run before comparison.

Validation: 53 distinct focused native cases passed, including concurrent index
publication, million-record bounds, shared cache rebinding, provenance and block
page boundaries, old archives, damaged pages, retained clipboard readers,
shared-store prefix protection, randomized edits, cache behavior and worker
viewport/hover requests. The 25 affected index/archive cases passed again after
the final shared-store prefix fix. Native app, tests and benchmark builds passed;
no full suite, Linux build or automated GUI integration run.

Reports: `dist/benchmarks/index-paging.json`, `index-open-{before,after}.json`,
`index-recovery-{before,after}.json`, and `index-edit-{before,after}.json`.

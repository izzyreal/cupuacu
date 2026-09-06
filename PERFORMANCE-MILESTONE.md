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

Remaining: progressive import still builds/retains its legacy UI and persistence
peak caches before paging; its import peak-memory growth is unchanged. The bounded
builder is available for the next import migration, which must preserve incremental
drawing and persistent-cache reuse. Source audio indexes, provenance/edit metadata,
aggregate overview accounting and the wider scheduling/resource work also remain.

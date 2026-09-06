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
legacy recovery conversion, new-document backend migration, larger M4A limits,
memory mapping and further scheduler unification remain outside this milestone.
The next step is manual verification of the delivered workflow, not another
architecture slice.

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

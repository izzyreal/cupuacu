# Revision backend integration status

Normal queued file opening now uses the disk revision backend.
`DocumentSession::bindReadRevision` binds imported/edited audio. Existing resident
restart histories and legacy synchronous loading retain
their compatibility paths until migration/removal.
A bound `Document` contains only shape and marker metadata. Legacy sample and
buffer access throws instead of returning placeholder silence. The session
reader supplies audio for background viewport requests, read-ahead playback
and ordinary streaming export.

Production cut, copy, delete, trim, reference paste, insert silence
and make-silent dispatch through immutable revisions when bound. Clipboard and
history pin source blocks, source coordinates and owned original files. Their
final releases go through the background reclaimer. Closing a tab does not clear
the clipboard; undoing copy/cut also leaves it intact. Paste redo pins the audio
originally pasted even after the clipboard changes. Commands restore selection,
cursor and marker snapshots; unlike the resident implementation's inverse
marker transforms, revision undo restores markers inside deleted ranges exactly.
Trim also restores its view snapshot. Failed commands retain redo history.

Reverse, gain/fade, dynamics, envelope and remove-silence jobs accept a pinned
revision. Generated effects process channels sequentially, using a 16,384-float
scratch block plus the builder's 65,536-float pending block. Peaks are generated
as output is written. Generated revisions share the application's decoded cache
with imports, recovery, recording and clipboard conversion. Remove-silence uses two
16,384-float scratch blocks, retains run descriptors and rearranges source
references; channel-only compaction pads with silence. Cancellation discards
partial output. Publication checks tab identity and the expected audio root.
History stores roots and editor metadata, not old/new full sample matrices or
whole-document waveform snapshots.

Single-sample edits use constant leaves and replace the same original root
throughout a drag; they neither fetch samples nor write sample files. Sample
handles consume published viewport values. Hover inspection uses available raw
viewport data or a latest-request asynchronous one-sample read. Missing data does
not trigger UI-thread disk reads. Normalize buttons run exact selected-range
analysis on a worker, reusing summaries and checking partial boundaries. Changes
to the document, selection, channels or settings discard obsolete results.

Mixed resident/revision clipboard pastes convert the accepted clipboard snapshot
on a worker, then validate the destination before committing. Resident-to-revision
conversion streams samples and compresses dirty/provenance runs; the reverse
bridge necessarily creates the legacy full clipboard segment. Reference pastes
share leaves, retain the existing sample-rate interpretation (no resampling),
drop excess source channels and pad missing destination channels with silence.
Clipboard changes during conversion do not change the accepted paste or get
overwritten by its publication. Clipboard lifetime after closing a tab is unchanged.

Remaining migration and resource work:

- Revision autosave, clipboard and matching restart history are integrated.
  Legacy snapshots and common histories now stream-convert to durable revisions.
  Histories with shape-changing recording commands retain compatibility recovery.
- Revision peaks persist with the archive and remain independently rebuildable;
  revision saves skip the resident waveform-cache rebuild/write after export.
- Saves independently retain the new output container before destination
  replacement, without decoding it again. Preserving output follows that
  container's encoding while unchanged audio/history roots retain their original
  representation. Foreign legacy clipboard provenance without retained source
  bytes cannot restore precision already lost to float.
- Bulk scheduling bounds execution and outstanding results. Declared job scratch
  displaces decoded samples within one shared budget (default 10% physical RAM).
  Paged audio indexes and comprehensive application memory accounting remain
  outstanding. Peak/index/run storage still grows with audio length or edit
  structure; effect reservations are not a total RSS guarantee.

`config/performance.json` accepts `{"audio_memory_mib": 256}` to override the
decoded-sample plus declared-scratch budget at startup. Missing or zero uses 10%
of physical RAM; invalid values are logged and retain the automatic default.
macOS memory-pressure notifications halve/quarter the cache target on a worker;
normal pressure restores capacity without eagerly reading data. SDL low-memory
events also request critical trimming. Running scratch reservations and reads
remain valid even if they temporarily exceed the reduced target. Scratch admission
uses the configured ceiling, not the temporary pressure target. Other desktop
platforms do not yet have native pressure monitoring.

Cache misses release the cache mutex before disk I/O. In-flight cache arrays count
against the same budget, including concurrent reads and failed reads. With no cache
slot available, reads go directly into caller buffers. Those caller buffers must
be budgeted separately. Existing effect scratch declarations participate in the
shared ceiling; decoder/DSP internals, transport, overview/index storage, resident
compatibility paths, cache metadata and undeclared job scratch are not covered.

Background save/overwrite now pins the revision, editor metadata and original
container before worker execution. Ordinary export uses the range reader;
preserving WAV/AIFF output streams source ranges, including PCM8/16/24/32 and
float32. Untouched compatible source samples retain their exact bytes, including
across endian changes and pasted channels. Generated samples are encoded in
bounded blocks. Sample/copy scratch is at most 256 KiB, plus at most eight open
source descriptors and their parsed metadata. RIFF/AIFF size limits produce
explicit errors. Output replaces the destination only after writing, flushing,
closing and the final cancellation check succeed. Source chunks and padding are
retained; marker chunks and frame/size fields are updated.

Save completion identifies the originating tab and document. Saving an older
revision cannot clear newer edits or close their tab. A saved revision remains
readable and undo/redo can return to its clean state. Completed save-job teardown
runs through the background reclaimer. Revision saves allow browsing, playback
and local edits while writing the pinned revision; later edits remain dirty.

Next slice: application-wide managed memory accounting and paged peaks/indexes.
Shape-changing legacy recording history and removal of the remaining resident
compatibility paths remain in the larger plan.

Focused validation: `[revision-ui],[revision-commands],[revision-effects]` covers production
splices against a flat sample model, history and clipboard lifetime, exact marker
restoration, blocked legacy sample access, effect parity across block boundaries
and channel selections, clipboard conversion metadata, point gestures, exact
normalization boundaries, cancellation cleanup and stale publication. Native
`edit_command_*` and `effect_{fixed,all}_*` benchmarks exercise the actual command
and effect job paths. `sample_command_*` and `normalize_*` cover point edits
and summary-based peak analysis against resident implementations. Broader platform, GUI and transport contention validation
belongs at the activation checkpoint.

`[revision-save],[streaming-export]` additionally exercise exact source precision,
all supported PCM widths, cross-endian/channel source ranges, opaque chunks,
markers and frame counts, cancellation, container limits, stale save publication,
and ownership after tab closure. `save_worker_*` compares resident and owned
save workers without post-save waveform persistence or UI publication.

Recording is integrated for bound mono/stereo sessions. The existing preallocated
callback queue feeds a second fixed 512-chunk SPSC queue through bounded UI
handoffs. A worker packs samples into segment files and publishes at most one
latest revision, with peaks constructed from the captured scratch, every 8,192
frames (about 186 ms at 44.1 kHz); Stop flushes the final partial batch. It does
not read or copy overwritten audio. UI publication changes the root and cursor;
one undo entry retains the original and final revisions. Save, mutation and tab
switching remain disabled until callback acknowledgement and final draining.
Input monitoring/playback retain the existing transport policy.

Recording's sample buffers are bounded: approximately 1 MiB handoff queue,
64 KiB interleaved scratch, at most 512 KiB builder scratch and a 1 MiB decoded
cache, in addition to the existing callback queue. Peak and sequence metadata
still grow with the recording; these are not paged yet. There is one owned
store per recording, not one directory/file per publication. Source ownership
follows document/history lifetime and final release happens on the reclaimer.

Queue overflow stops capture before a gap; write failure keeps only the already
published prefix, reports the error and supplies one undo entry for that prefix.
Failed later appends do not prevent reading previously flushed blocks. A closed
or replaced document cancels publication while its remaining input is drained.
Completed recording revisions now participate in document autosave; capture
in progress is not checkpointed. New documents also use this recording path.
`[revision-recording]` exercises the
real callback/drain, overwrite/extension, mono/stereo, exact samples and peaks,
undo/redo, overflow, write failure and stale publication without audio devices
or GUI automation. `record_*_owned` measures fixed-work and growing-work scaling.

## Durable revision checkpoints

Bound sessions autosave immutable roots, saved-state identity, editor metadata
and matching undo/redo together. Snapshot capture retains references; a worker
writes newly encountered sequence nodes and source records to a checksummed,
append-only index, then atomically replaces a small version-1 manifest. Data and
index files are flushed before manifest publication. Unchanged nodes and source
stores are reused. Peaks use binary 64 KiB pages instead of JSON sample arrays.

Each document or clipboard archive independently owns its sample segments and
original source container. Initial acquisition attempts filesystem cloning on
macOS, with bounded copying as fallback; later appends copy only new bytes.
This can duplicate logical storage across archives and requires initial I/O on
filesystems without cloning. Original precision survives recovery and preserving
export, including PCM32 bits not representable in float32.

Startup restores the committed audio, view and matching history rather than
combining it with newer session-list metadata. Normal recovery reads indexes and
peaks without scanning sample data. Damaged peak pages can be rebuilt from owned
audio on the recovery worker; damaged required revision records fail recovery
without replacing the destination session. Rebuilt peak pages are not immediately
rewritten. Resident version-2 snapshots now migrate to revision archives during recovery;
shape-changing legacy recording history retains its existing reader.

The existing 512 MiB restart-history retention limit counts distinct stores
required only by history, excluding current/saved document stores. Exceeding it
retains the document and reports omitted restart history. Unsupported legacy
commands likewise produce an explicit history warning. In-memory history is not
evicted. Point and revision commands restore their shared before/after roots;
metadata-only marker commands retain their existing serialization.

Autosave tracks history-only changes and stale worker completion separately.
Saving updates the saved root while retaining restart history. Write failures
report an error and retry after a delay; failure before manifest replacement
leaves the previous checkpoint readable. Closed archives reject late publication.
Final release and store reclamation happen through the background reclaimer.
Clipboard replacement prunes obsolete stores after their last reader releases;
closing a document still leaves the clipboard intact.

Limits: checkpoint capture and manifest serialization still scale with history
metadata. Document archives retain unreachable records/stores until removal;
clipboard archives prune stores but do not compact old index records. Malformed
manifests with unidentifiable generations retain their sidecar data for recovery.
Indexes and recovered peaks remain resident. There is no global memory admission
policy or power-loss/platform validation claim in this slice. Existing shutdown
flush and job coordination still apply.

`[revision-persistence]` covers restart history/root identity, exact original
precision, stale autosave, copy-only history, save/dirty-state transitions,
retention limits, injected write failure, corruption, cancellation, close during
checkpoint preparation and clipboard reclamation with live readers.
`checkpoint_*` and `recovery_owned` benchmarks measure persistence separately
from import, GUI rendering and sample validation.

## Normal file-opening activation

Background opening owns the original container by cloning on macOS or bounded
copying, decodes into packed sample segments and generates peaks in the same
pass. It publishes metadata first, then bounded peak batches. The preview has
external shape metadata only; neither the decoder sink nor the preview allocates
resident sample-page tables. Unavailable detailed samples remain pending and
never invoke resident sample reads while painting or hovering.

Imports share one decoded-sample cache, initially budgeted to 10% of physical RAM.
Samples enter this cache only on reads; this is not an application-wide RAM limit.
Peak summaries, indexes, decoder scratch and other job caches remain separate.
Cached peaks are checked against the original filename and reused during import.
New peaks are persisted on the opening worker before its completed result is
published. Completion binds the immutable revision directly; worker destruction
is deferred rather than joined on the UI thread. Existing modal interaction rules
remain until the scheduler/operation-state slice.

Saved-container ownership writes a private container, then clones or copies it
to a temporary destination before replacement. Cancellation or copy failure
leaves the existing destination and session reference intact. Both synchronous
compatibility saves and production background saves use this ownership rule.
Changing WAV/AIFF encoding updates the preservation reference without rebasing
undo roots or allocating another decoded recording. Systems without filesystem
cloning incur a second output-sized copy; source-byte ownership has a real disk
space and I/O cost.

`[revision-activation]` checks production opening, cache reuse, source-file
independence, reference undo, format-changing Save As, subsequent preserving
overwrite/recovery and failed output publication. Native `open_uncached` cases
compare WAV, ALAC and FLAC; `save_worker_*_owned` covers save overhead. Loading
completion can be slower because decoded data is now written to disk. This
checkpoint does not claim that large-file import throughput has improved.

## Shared bulk scheduling and document operations

Each application State owns a lazy two-worker bulk scheduler for open, save,
effect and autosave jobs. The pending queue is limited to 64 entries, and at most
66 accepted jobs may remain running, queued or awaiting publication/reclamation.
Admission failure is reported explicitly. User jobs precede maintenance; autosave
receives a two-second queue deadline that promotes it ahead of new user work.
Running work is cooperative and is not preempted. Transport read-ahead and latest
viewport services retain independent workers, so bulk jobs cannot take their
execution slots. This does not reserve filesystem bandwidth.

Effects reserve estimated channel scratch before execution against a 128 MiB
bulk scratch allowance. Other allocations, cache residency, peak/index data and
queued payload bytes are not yet charged to one shared budget. This scheduler
is the admission mechanism for the next resource slice, not a completed global
memory policy. Clipboard conversion and peak-analysis services also retain their
existing dedicated workers.

Normal imports and revision effects/saves have tab-specific operation identities
and a nonmodal progress footer. Imports/effects prevent mutation of their own
document; another tab remains editable. Save pins the old revision and permits
local edits. A second bulk operation on the same tab is rejected. Closing a tab
invalidates its publication target. An accepted save still finishes its pinned
output after tab closure, preserving the existing save contract. Completed jobs release through the reclaimer
without joining workers during normal UI pumping. Shutdown drains writes before
its final checkpoint. Autosaves already writing an archive finish their current
transaction; they do not yet support cancellation inside that transaction.

Opening publishes sealed sample blocks as well as peaks. Browsing can request raw
views of the available prefix through the asynchronous viewport worker. Requests
beyond it stay pending. Playback snapshots the available prefix at Play, retains
that data independently and ends at that snapshot's boundary; starting playback
again uses the newly available audio. Likewise playback begun during an effect
retains the old revision. The existing restriction on switching tabs while playing
or recording remains. Legacy resident operations and startup recovery retain
their modal compatibility paths. Import cancellation removes only its own
placeholder tab and preserves unrelated tab edits. Clipboard lifetime is unchanged.

Focused checks: `[scheduler],[document-operations]` cover concurrency, scratch and
result admission, priority/deadlines, exception isolation, queued cancellation,
closed targets, unrelated edits and progressively readable audio.
`bulk_busy_edit_owned` measures effect submission and 128 point edits in another
tab while both bulk slots remain occupied. It reports edit p99 and verifies both
histories; this is headless command latency, not SDL event-loop latency. Existing
open/effect timing scenarios track throughput separately.

### Reusable decoded imports

Normal queued opening consults `state/decoded-cache` before decoding. This is an
optional, versioned cache of completed original imports, using revision archives
for samples, peaks, metadata and original container bytes. Edits never alter its
contents. A weak live index supports immediate reopening while persistence is
pending; otherwise reopening reads the archived revision without decoding audio.
Restored samples use the application's shared sample cache.

The default reusable disk budget is 8 GiB (`State::decodedImportCacheByteBudget`;
zero disables it). Admission estimates sample/source/index space, retains at least
1 GiB free disk space, and skips oversized imports. Old unused entries are evicted
in order of manifest access time. Archives with live readers are skipped; closing
a tab cannot invalidate audio held by a clipboard, history or another reader.
Writes use one maintenance job at a time on the existing bulk scheduler, avoiding
a queue of retained recordings or two workers waiting on cache writes. If busy,
full or unavailable, persistence is skipped and normal import remains valid.
Working data still needed by documents is separate from this cache budget.

Source identity includes canonical path, size and modification time; POSIX also
includes device/inode and nanosecond change time. Cache keys and manifests must
match, and archive metadata and segment lengths are validated on restoration.
Invalid or incomplete entries fall back to decoding. A cache-directory lease
prevents another process from evicting active files; a competing process imports
normally. Cache-backed revisions retain that lease through their final release.

Cache creation runs after audio becomes editable. APFS cloning avoids copying
sample/source payloads where possible; other filesystems use the archive's bounded
copy fallback. Archive copying snapshots the immutable prefix under the store
lock, then performs file I/O outside it so read-ahead need not wait for copying.

Reopening assigns a fresh process-local preservation identity to the imported
revision and its document. Persisted numeric IDs from earlier application runs
must not identify unrelated current documents during legacy clipboard conversion.
The original container bytes, markers and sample values remain unchanged.


New documents created through the New File command start with an empty saved
`AudioEditRevision` in the selected format. Silence insertion, point edits,
clipboard operations, effects and recording therefore use the same revision
paths as imported documents. Recording starts with a readable empty revision,
publishes disk-backed blocks and peaks, and finishes with one history entry;
undo returns to the empty saved revision. Unconfigured startup tabs adopt a
revision when first receiving a revision clipboard. Most legacy recovered documents now migrate to revisions. Shape-changing
legacy recording histories retain their compatibility backend.


Detailed revision peak levels now use disk pages via `SourcePeaks::createPaged`.
Each page groups a spatial subtree of summary levels to avoid cache thrashing;
it shares the decoded-sample cache and budget. Levels with at most 4,096 values
remain resident. `queryBlocks`/`readPeaks` on such sources may read disk and belong
on workers; the ordinary SourcePeaks constructor remains memory-only for legacy
UI-side cache snapshots. Durable archive format compatibility is unchanged.

Recovery, effects and clipboard conversion now stream base summaries through
`StreamingPeakBuilder`; archive loading rebuilds higher levels from streamed base
records. These paths no longer materialize a full peak pyramid.

Progressive import feeds the same sample reducer into `ProgressivePeaks`. One
active spatial tile per channel/group accumulates base peaks and parent summaries;
completed tiles are packed into temporary segment files. The final `SourcePeaks`
reader shares those files, so completion neither copies nor repages the pyramid.
Viewport workers query the published prefix under a read lock; the UI sees only
shared readers and atomic availability. Notifications coalesce, and pending
waveform regions never trigger synchronous reads. A completed request refreshes
the texture; later availability schedules another request without canceling the
one already running.

Persistent v1 caches are loaded with bounded base-peak reads and saved by streaming
the immutable reader. Existing cache files remain compatible. A cached overview
may cover the whole document before sample decoding finishes; raw views/playback
still respect available samples. Cache corruption falls back to generating peaks
with decoding. Legacy resident-cache APIs remain for compatibility paths.

Active tiles and small per-source overviews are bounded per source but are not
charged to aggregate admission. Audio block indexes and provenance/edit metadata
also remain outside the shared bound. See the milestone report for measured
memory reduction and I/O/latency costs.

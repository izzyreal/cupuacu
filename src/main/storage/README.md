# Revision backend integration status

The disk revision backend is staged; it is not the default file-opening backend.
`DocumentSession::bindReadRevision` explicitly binds imported/edited audio.
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
as output is written. Generated revisions share one 1 MiB decoded cache across
jobs/history, separately from imported-data caches. Remove-silence uses two
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

Remaining before default activation:

- Revision autosave, clipboard and matching restart history are integrated.
  Legacy resident snapshots retain their existing reader; automatic conversion
  of legacy recovery data into revisions is not implemented.
- Revision peaks persist with the archive and remain independently rebuildable;
  revision saves skip the resident waveform-cache rebuild/write after export.
- Preservation uses the independently owned import container as its metadata
  reference. Rebinding that reference after a format-changing generic Save As
  remains work for activation; preservation explicitly rejects a target that
  does not match the retained container. Foreign legacy clipboard provenance
  without retained source bytes cannot restore precision already lost to float.
- Shared scheduling/admission, paged indexes/peaks and application-wide memory
  accounting remain outstanding. Peak/index/run storage still grows with audio
  length or edit structure. The bounded effect scratch/cache is not a total RSS
  guarantee, and the existing effect job coordination still applies.

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
runs through the background reclaimer. Existing global long-task coordination
still restricts user interaction while saving; this does not implement the
planned document-level scheduler.

Next slice: owned-container rebinding after format conversion, followed by a
focused default-activation checkpoint. Global scheduling, transport reservations,
paged peaks/indexes and application-wide memory accounting remain in the larger
plan; durable persistence does not complete those requirements.

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
in progress is not checkpointed. The revision backend remains staged.
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
rewritten. Resident version-2 snapshots remain readable through their existing
path. Revision format migration is not a legacy-to-revision converter.

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

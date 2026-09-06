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

- Reference clipboard and undo restart manifests are not implemented; no legacy
  snapshot is advertised for reference history.
- Recording and recovery still need revision integration. Bound sessions do not
  schedule legacy autosave snapshots. Default resident sessions retain their
  existing persistence and clipboard behavior.
- Revision peak persistence remains independently rebuildable; revision saves
  skip the resident waveform-cache rebuild/write after export.
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

Next slice: recording and durable revision/clipboard/recovery manifests, plus
owned-container rebinding after format conversion. Default activation follows
coverage of those consumers; global scheduling, transport reservations, paged
peaks/indexes and application-wide memory accounting remain in the larger plan.

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

# Revision backend integration status

The disk revision backend is staged; it is not the default file-opening backend.
`DocumentSession::bindReadRevision` explicitly binds imported/edited audio.
A bound `Document` contains only shape and marker metadata. Legacy sample and
buffer access throws instead of returning placeholder silence. The session
reader supplies audio for background viewport requests, read-ahead playback
and ordinary streaming export.

Production cut, copy, delete, trim, same-format reference paste, insert silence
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

Remaining before default activation:

- Sample-point dragging and dialogs that synchronously inspect samples (such as
  peak/normalization preparation) still need revision-aware UI paths.
- Clipboard conversion between resident/revision backends and differing audio
  formats is explicitly unavailable in staged sessions. Reference clipboard
  restart manifests are not implemented; no legacy snapshot is advertised.
- Preservation writers must resolve original source byte ranges through revision
  leaves. Original bytes remain owned; preservation saving is explicitly
  unavailable for bound sessions until that writer exists.
- Background saves, recording and recovery need revision integration. Bound
  sessions do not schedule legacy autosave snapshots. Default resident sessions
  retain their existing persistence and clipboard behavior.
- Shared scheduling/admission, paged indexes/peaks and application-wide memory
  accounting remain outstanding. Peak/index/run storage still grows with audio
  length or edit structure. The bounded effect scratch/cache is not a total RSS
  guarantee, and the existing effect job coordination still applies.

Focused validation: `[revision-commands],[revision-effects]` covers production
splices against a flat sample model, history and clipboard lifetime, exact marker
restoration, blocked legacy sample access, effect parity across block boundaries
and channel selections, cancellation cleanup and stale publication. Native
`edit_command_*` and `effect_{fixed,all}_*` benchmarks exercise the actual command
and effect job paths. Broader platform, GUI and transport contention validation
belongs at the activation checkpoint.

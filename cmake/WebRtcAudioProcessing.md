# WebRTC Audio Processing dependency

Cupuacu fetches the `pulseaudio/webrtc-audio-processing` v2.1 source tree from
its official freedesktop Git repository. `CMakeLists.txt` pins the source
directly to commit `846fe90a289f58b7c9303a635142aa2c7caa93e5`, and
`WebRtcAudioProcessing.cmake` exposes the required subset as
`cupuacu::webrtc_apm`.

`WebRtcAudioProcessingSources.cmake` is an explicit source closure for AEC3
and `PushSincResampler`. It intentionally excludes the rest of the upstream
Audio Processing Module, including AGC, AECM, noise suppression, VAD, codecs,
video/API utilities, and tests. The manifest includes only the SSE/AVX files
referenced by the portable code's x86 runtime dispatch; generated wrappers
keep those files out of non-x86 slices in universal builds. Do not replace the
manifest with a recursive glob: compiling the entire snapshot makes unrelated
upstream modules accidental dependencies.

Do not copy or patch the upstream source inside Cupuacu. To update it:

1. Choose a reviewed upstream release and pin its peeled tag or commit.
2. Update the reference and documented commit in `CMakeLists.txt` and here.
3. Regenerate and review the link dependency closure, then update
   `WebRtcAudioProcessingSources.cmake`. Compare it with the upstream AEC3 GN
   target and Cupuacu's direct resampler use.
4. Review compile definitions and platform libraries in
   `WebRtcAudioProcessing.cmake`.
5. Configure from an empty build directory, run the full unit suite plus RTSan
   and TSan, and perform the hardware validation described in `DEV.md`.

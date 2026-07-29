# WebRTC Audio Processing dependency

Cupuacu fetches the `pulseaudio/webrtc-audio-processing` v2.1 source tree from
its official freedesktop Git repository. `CMakeLists.txt` pins the source
directly to commit `846fe90a289f58b7c9303a635142aa2c7caa93e5`, and
`WebRtcAudioProcessing.cmake` exposes the required subset as
`cupuacu::webrtc_apm`.

Do not copy or patch the upstream source inside Cupuacu. To update it:

1. Choose a reviewed upstream release and pin its peeled tag or commit.
2. Update the reference and documented commit in `CMakeLists.txt` and here.
3. Review the source selection and compile definitions in
   `WebRtcAudioProcessing.cmake`.
4. Configure from an empty build directory, run the full unit suite plus RTSan
   and TSan, and perform the hardware validation described in `DEV.md`.

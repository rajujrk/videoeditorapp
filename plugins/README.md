# Plug-in hosts

Built (Phase 5, slices 19a and 19b). The hosts live with the engine rather than here:

- **Audio Units (AUv2 and AUv3) and VST3** — `engine/audio/plugins/` (`AudioUnitPlugin.mm`,
  `Vst3Plugin.mm`, `AudioPlugin.h`): effects on a clip or a whole track, parameters as 0..1 so
  keyframes mean the same for any plug-in, their state saved with the project, latency made up.
- **OpenFX 1.5** — `engine/video/OfxHost.mm` and `OfxFrames.mm`: image effects from
  `/Library/OFX/Plugins`, rendered on the GPU frames, their parameters shown as the app's own
  controls.
- **Scanning** — `tools/pluginscan`: every bundle is checked in a process of its own, so a plug-in
  that crashes can't take the app with it (installed beside the app as `tve-pluginscan`).
- **Tests** — test plug-ins of our own (`tests/vst3/TestGain.cpp`, `tests/ofx/TestInvert.mm`), the
  engine tests (`test_audio_plugins.cpp`, `test_ofx.cpp`) and `tests/qml/tst_video_plugins.qml`.

This folder only points there.

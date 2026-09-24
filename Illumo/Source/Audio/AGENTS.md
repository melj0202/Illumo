# Illumo audio subsystem guidance

This directory holds sound effects (D-E28): `AudioDecoder.cpp` (clips from
WAV, FLAC and MP3 bytes) and `AudioDevice.cpp` (the native `IAudio` mixer).
Public contracts are `Illumo/Include/Illumo/Audio/*`. `AudioDecoder.cpp` also
builds into the guest library `IllumoGuestRendering`.

## Invariants

- miniaudio (`Illumo/thirdparty/miniaudio-0.11.25`) is private to this
  directory. No public header includes it or names a miniaudio type;
  products and other subsystems see only `IAudio`, `AudioClip`,
  `AudioDecoder` and `AudioDevice`.
- `AudioDecoder.cpp` is miniaudio's only implementation unit (it defines
  `MINIAUDIO_IMPLEMENTATION`). Do not compile `miniaudio.c` or define the
  implementation elsewhere. Feature switches come from the target
  (`ILLUMO_MINIAUDIO_DEFINITIONS` natively; the guest set in
  `IllumoGuest/CMakeLists.txt`) so every unit agrees on struct layouts.
- Decode only through miniaudio's memory entry points (`ma_dr_*_memory`).
  `ma_decoder` and file-path loaders link libc stdio, which adds WASI file
  imports (`path_open`, `fd_read`) that the sandbox refuses; the game package
  then fails to instantiate.
- Guests decode their own files and send only samples. The host must never
  decode a guest's encoded audio.
- `IAudio` calls are main-thread affine. `AudioDevice` voices never move
  while the mixer thread may read them; release a voice (`ma_sound_uninit`)
  before reusing it or freeing its sound's samples.
- Keep clip and table bounds (`AudioClip::kMaximumSamples`,
  `IAudio::kMaximumSounds`, `IAudio::kMaximumVoices`) in step with the Audio
  service decoder and the per-guest budget in `WasmGameServices`.

## Verification

`Illumo.Audio.*` (decoder and a headless `AudioDevice`, no sound card
needed), `Illumo.Wasm.AudioServiceDecoder`, `Illumo.Wasm.AudioServices`,
`Illumo.Wasm.GuestAudio` and `IllumoGame.Wasm.GamePackageAudio`. Real output
needs a manual smoke on a machine with a device. Record:
`docs/audio-subsystem-plan.md`.

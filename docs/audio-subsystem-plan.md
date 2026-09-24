# Audio subsystem and CSim sound cues

Status: implemented and validated 2026-09-24 on `release/v26.09`
(uncommitted). Decision: D-E28. Canonical description:
[architecture-consensus.md §5.13](architecture-consensus.md).

## 1. Objective and end state

The owner asked for an audio subsystem built on an external library, wrapped
so that other components call it as an ordinary Illumo component, and for the
sound effects in `IllumoGame/Assets` to be hooked up.

End state:

- one engine seam, `IAudio` (`IllumoContext::audio`), with no library type in
  any public header;
- a native implementation (`AudioDevice`) and a decoder (`AudioDecoder`)
  wrapping the library;
- guests (every product ships as WASM) reach it through a new optional
  capability and service, decoding their own files inside the sandbox;
- CSim plays seven cues, with a persisted volume setting.

Non-goals: music streaming, positional (3D) audio, recording, a mixer bus
hierarchy, audio in IllEd or IllMeshViewer (they are granted the capability
but play nothing), Linux validation.

## 2. Dependency assessment (owner approval: the request itself)

| Aspect | Assessment |
|---|---|
| Choice | miniaudio 0.11.25 (2026-03-03), `github.com/mackron/miniaudio`, tag `0.11.25`. Alternatives: SDL_mixer (needs SDL, several libraries), OpenAL Soft (LGPL, shared library), PortAudio (output only, no decoders), FMOD/Wwise (proprietary licenses). miniaudio covers decoding, resampling, channel conversion, mixing and every Windows backend in one header. |
| License | Public domain (Unlicense) or MIT No Attribution, at the user's choice. No attribution is required; the notice is still recorded and staged. |
| Maintenance | Actively maintained single author project with regular releases; the vendored files are verified against the upstream git blob hashes of the tag (`miniaudio.h` `c6d493ee`, `LICENSE` `2f9423ad`). Updating means replacing one header. |
| Build | Header only, compiled once in `Illumo/Source/Audio/AudioDecoder.cpp`. Natively it needs no link libraries (Windows backends are loaded at run time). It compiles unchanged for `wasm32-wasip1` with device, threading, engine and node graph code disabled. |
| Deployment | No DLL; `miniaudio-LICENSE.txt` is staged under `licenses/`. The native runtime gains the mixing engine and decoders; guest modules gain only the decoders they reach (`IllumoGame.wasm` is 6.9 MB). |

## 3. Design

See §5.13 of the consensus for the full contract. Key choices:

- **Service, not import.** The ABI has no product-visible host imports; every
  host service is a record in the Services envelope. Audio follows the
  clipboard/window pattern: capability bit 11, `GuestService::Audio` (18),
  strict `GuestAudioRequest` decoder, deny paths that reject without retiring
  the guest, malformed requests that fail the exchange.
- **Decode in the guest.** Textures are decoded guest-side so the host never
  parses untrusted media; audio does the same. Only validated float samples
  cross, bounded per clip (3 Mi samples, one service record) and per guest
  (64 MiB).
- **Fire-and-forget.** Requests complete in the same exchange and the guest
  queue discards Audio completions, as it does Log's, so plays never hold
  outstanding-request slots.
- **Optional everywhere.** The host grants `Audio` only with an output and
  never for captures or benchmarks; products treat `IllumoContext::audio` as
  optional, like `panelSurfaces`.
- **Product seam.** `CSimSounds` is installed for the store's lifetime, as
  `CSimPlatform::current()` is, because menus and dialogs have no context. It
  counts cues even when silent so the native tests can check wiring.

## 4. Implementation record

| Area | Change |
|---|---|
| Engine | `Illumo/Include/Illumo/Audio/{Audio,AudioClip,AudioDevice}.h`, `Illumo/Source/Audio/{AudioDecoder,AudioDevice}.cpp`, `IllumoContext::audio`, `Illumo/Source/Audio/AGENTS.md` |
| ABI and host | `Protocol.h` (bit 11), `Services.h` (op 18, completion discard), `IllumoGuest/Audio.h` (wire record, `GuestAudio`), `WasmGameServices` (validate, execute, bound, release), `WasmGameModule::setAudio`, `RuntimeApplication` (device ownership) |
| Guest SDK | `IllumoGuest/Source/Audio.cpp`, `GuestModuleApplication` publishes `GuestAudio` when granted |
| CSim | `CSimSounds`, cues in `MainMenuModule`, `ConfigurationMenu` (new Sound volume row), `NewSimulationMenu`, `ExitConfirmDialog`, `RulesetWorkshopMenu`, `CellGameModule`; bank loading in `GameApplication`; `soundVolume` default 80 |
| Build | miniaudio definitions per target, license staging, `Sounds/*.wav` staged only when present (the sources are git-ignored) |

## 5. Deviations and findings

- The first decoder used `ma_decoder`, whose backend tables reference
  miniaudio's stdio file loaders. The game module then imported
  `path_open`, `fd_read`, `fd_fdstat_get` and `fd_fdstat_set_flags`, which
  the sandbox refuses, so the package failed to instantiate
  (`IllumoGame.Wasm.GamePackage` caught it). The decoder now calls the
  embedded `ma_dr_wav`/`ma_dr_flac`/`ma_dr_mp3` memory entry points, which
  are declared only in miniaudio's implementation section; that is why
  `AudioDecoder.cpp` is the implementation unit and upstream `miniaudio.c` is
  not vendored. The game module's imports are back to the host's WASI stub
  set.
- Adding the settings row moved Apply/Discard/Exit down one row; four tests
  that navigate by key count were updated (`ConfigurationMenu.Navigation`,
  `MainMenu.SettingsApply`, `MainMenu.MouseIsolation`,
  `CellGame.ReleaseConfiguration`).

## 6. Validation (2026-09-24, Windows 11, MSVC Release, wasi-sdk 34)

- Full Release workspace build; its test run passed 631/631. The later
  targeted CTest run of every Wasm, runtime, audio and affected game case
  passed 128/128, plus `IllumoGame.Wasm.GamePackageAudio`.
- New cases: `Illumo.Audio.{DecodeWav,DecodeRejects,ClipValidity,DeviceMixes,DeviceLifetimes}`
  (headless mixing, no sound card), `Illumo.Wasm.{AudioServiceDecoder,AudioServices,GuestAudio}`
  (37 assertions: decoder grammar, deny/permit, budget, release on cancel,
  real guest queue round trips), `IllumoGame.Sounds.Bank`,
  `IllumoGame.ConfigurationMenu.SoundVolume`, cue assertions in
  `MainMenu.Navigation` and `CellGameModule.CanvasReturn`, and
  `IllumoGame.Wasm.GamePackageAudio` (the real `IllumoGame.wasm` through the
  generic host: seven sounds registered, start/hover/enter/exit cues, 80%
  volume, release on exit).
- Import audit: `IllumoGame.wasm` imports exactly the WASI stubs the host
  defines (same set as `IllEd.wasm`).
- clang-format on every changed C++ file; clang-tidy (workspace config) clean
  on every changed translation unit.
- Manual smoke: `IllumoRuntime` (Release, default device) ran the game for
  14 s with no `Audio disabled` or `Sound unavailable` warnings in the log, so
  the device opened and all seven owner WAVs (16-bit PCM and 32-bit float,
  44.1 and 48 kHz, one with a `JUNK` chunk) decoded and registered. Audible
  output was not observed by the agent.
- Not run: Debug/ASan configuration, coverage, Linux (the free disk space on
  C: was under 0.5 GB during validation).

## 7. Follow-ups

- Open the output device lazily or only for apps that register sounds; IllEd
  and IllMeshViewer are granted `Audio` and keep an idle device open.
- Owner decision (2026-09-24): sound files are not tracked. `IllumoGame/.gitignore`
  ignores everything under `IllumoGame/Assets/`; supply the WAVs locally, or a
  fresh checkout builds a silent game.
- Music or long ambience would need streaming rather than whole-clip
  registration (clips are capped near 34 s of stereo).

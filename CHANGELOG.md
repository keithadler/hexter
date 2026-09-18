# Changelog

## 2.2.0 (2026-09-17)

### Added
- An Android app: the engine behind an Oboe low-latency stream, USB MIDI in through Android's
  MIDI service with sysex reassembled so bank dumps load, the six bundled banks plus any bank
  file on the device, a voice list, volume, a level meter and a two-octave keyboard on the
  screen. Android 8.0 or newer, arm64 and x86_64. Built in `android/` with Gradle and the NDK
  from the same engine sources; CI builds it, the release workflow signs and attaches it.
  Asked for on theabolton/hexter#18.

## 2.1.2 (2026-09-17)

### Added
- Release packages for Linux arm64 and Windows on ARM, built on GitHub's ARM runners: every
  plugin, the standalone and the render tool, the same as the x86 packages. The macOS
  package was already universal. Asked for on theabolton/hexter#18.
- The release workflow can be run by hand as a dry run: it builds and packages every
  platform and keeps the packages as workflow artifacts, without touching a release.

## 2.1.1 (2026-09-14)

### Fixed
- The standalone stopped at startup with "Unable to configure audio: RtApi::getDeviceInfo:
  deviceId argument not found" on a Windows machine (reported by Reaper10 on
  theabolton/hexter#18). RtAudio had answered 0 for the default output, which happens when the
  default endpoint fails its probe or there is no capture device, and clap-wrapper asked for
  device 0. A patch applied to clap-wrapper at build time (`cmake/`) now falls back to the
  first device with outputs, and keeps the window and MIDI up without sound when there is
  none, so a device can be chosen in Audio/MIDI Settings. DirectSound is compiled in beside
  WASAPI as a second API to choose from.

## 2.1.0 (2026-09-13)

### Added
- A standalone application, `hexter.app` (macOS), `hexter` (Linux) and `hexter.exe`
  (Windows): the CLAP in a window of its own with audio and MIDI I/O, through clap-wrapper
  with RtAudio and RtMidi. It opens on the default output, listens on every MIDI input, and
  has an Audio/MIDI Settings panel. No DAW needed. Asked for on theabolton/hexter#18.

### Changed
- The Windows release is built with MSVC instead of MinGW, because clap-wrapper's Windows
  standalone shell is C++/WinRT. The C runtime is linked statically, so the zip still needs
  nothing installed. MinGW builds keep working (CI checks them) and skip the standalone.
- The engine's mutex is behind `hexter_mutex.h`: pthreads where they exist, an SRWLOCK on
  MSVC, which has no pthread.h.

## 2.0.2 (2026-09-12)

### Fixed
- Windows: the CLAP, the LV2 plugin and `hexter-render.exe` in the 2.0.0 and 2.0.1 zips needed
  `libwinpthread-1.dll`, MinGW's pthreads runtime, which a Windows machine has only when
  MinGW is installed. The plugins failed to load and the tool stopped with
  "libwinpthread-1.dll was not found" (reported by Reaper10 on theabolton/hexter#18). The MinGW runtime is now linked statically,
  so the zip stands on its own.

## 2.0.1 (2026-09-08)

### Added
- Audio Unit (`hexter.component`, macOS) built from the CLAP through
  clap-wrapper 0.16.0, for Logic Pro and GarageBand. Passes `auval`.

## 2.0.0 (2026-09-07)

The revival release. Same engine, new plugin formats, new build.

### Added
- CLAP plugin (`hexter.clap`): parameters, state, `clap.preset-load` for
  bank files, CLAP and MIDI note dialects, sysex.
- LV2 plugin (`hexter.lv2`): MIDI in, control ports, `patch:Set` bank
  loading through the worker extension, state.
- `hexter-render`: renders notes or Standard MIDI Files to WAV with no host.
- `src/hexter_engine.h`: a host-independent engine API.
- Sysex support over MIDI: DX7 32-voice bulk dump, single voice dump,
  voice parameter change (live on playing notes for operator parameters),
  function parameter change.
- `HEXTER_DEFAULT_BANK` (and the older `HEXTER_DEFAULT_PATCH`) environment
  variable loads a bank on instantiation.
- Tests: bank formats, sysex, rendering, voice management, state, CLAP
  host-side, LV2 host-side (with lilv).
- GitHub Actions CI on Linux, macOS and Windows with `clap-validator` and
  `lv2_validate`; release builds attached to tags.

### Changed
- CMake replaces autotools. `configure.ac`, `Makefile.am`, `aclocal.m4`,
  `autogen.sh`, `config.h.ac` and `INSTALL` are gone.
- The engine no longer includes `ladspa.h`, `dssi.h` or ALSA headers. The
  DSSI plugin and GTK2 editor are optional targets built when their
  libraries are found.
- `INT_TO_FP` multiplies instead of left-shifting, since shifting a negative
  value is undefined in C.
- `README.rst` moved to `docs/README-1.1.rst`; `README.md` is new.

### Fixed
- Crash (integer division by zero) at unusual sample rates such as 1234.5 Hz:
  the envelope slew limit overflowed the fixed-point range and the ceiling
  division overflowed 32 bits. Found by `clap-validator` on x86; Apple
  silicon saturated instead of trapping.
- Oscillator phase accumulation wraps in unsigned arithmetic instead of
  relying on signed overflow.
- Bank import over sysex now refreshes the sounding patch
  (theabolton/hexter#9).
- A bank loaded at an offset near the end of the 128 slots is truncated
  instead of overrunning.

## 1.1.1 (2021-01-01)

See [ChangeLog](ChangeLog) for the history of 0.5 through 1.1.1.

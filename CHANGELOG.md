# Changelog

## 2.5.1 (2026-09-20)

### Fixed
- The Linux standalone in 2.5.0 linked JACK, which means it refuses to start at all on a
  machine with no JACK library installed. That was a regression against 2.4.1, which needed
  only libraries every Linux has. The standalone in the package is built without JACK again
  and runs anywhere; a second binary, `hexter-jack`, carries JACK for people who want it and
  have it. Both workflows build and check both, so neither can quietly turn into the other.

## 2.5.0 (2026-09-19)

### Added
- The Linux standalone speaks **JACK** as well as ALSA and PulseAudio. It always could in
  principle, since the audio layer compiles in whatever backends it finds, but the build
  machines had no JACK development package, so every Linux release so far shipped without
  it and nothing said so. The package is installed now, and both workflows fail if the
  built standalone turns out to be missing a backend, so it cannot go quiet again.

## 2.4.1 (2026-09-19)

### Fixed
- TX81Z banks now actually load. 2.4.0 claimed them and did not have them: a TX81Z bank is
  the same dump as a DX100's under format byte `0x04` rather than `0x03`, and only `0x03` was
  recognized. Both are accepted now. Its voices sit the same way in the bytes hexter reads, so
  they convert like the others and the TX81Z's extras are ignored. This part is built from the
  format rather than from a real dump and has not been tried against hardware.

## 2.4.0 (2026-09-19)

### Added
- **DX21, DX27 and DX100 banks load.** A four-operator 32-voice dump is recognized and each
  voice is converted to a six-operator one on the way in: the four operators become four of
  the six, wired up with whichever DX7 algorithm stands closest to the four-operator one, and
  the other two operators stay silent. Envelopes, frequency ratios, detune, feedback, the LFO
  and the name come across.

  This is a **sound-alike, not the real thing**. The four-operator machines are a different
  synth, so a converted voice is an interpretation: their envelopes have a stage the DX7 does
  not, their pitch modulation reaches further for the same number, and the TX81Z's operator
  waveforms have nowhere to go. Play them, do not expect your hardware.

  The voice memory layout and the algorithm mapping follow [XDX](https://github.com/wurly200a/xdx)
  by Wurly, MIT licensed, which Reaper10 found. See AUTHORS. Asked for on theabolton/hexter#18.

## 2.3.0 (2026-09-18)

### Added
- An **Algorithm** parameter on the CLAP and LV2 plugins, and so on the standalone and
  the Audio Unit as well. It forces the patch's operator wiring, the knob the hardware
  has on its front panel. It is an override, not a setting: at **Patch**, where it rests,
  every program plays its own algorithm. Move it to 1-32 and that wiring is forced, which
  is heard under notes already held; move it back, or select a program, and the patch's
  own algorithm returns. Asked for on theabolton/hexter#18.
- The engine gained `hexter_engine_get_voice_parameter()` to read a single voice
  parameter back, and voice parameter 134 (the algorithm) now reaches playing voices the
  way operator parameters already did.
- A project page at https://keithadler.github.io/hexter/. It reads the current release
  from GitHub when it loads, so its version and download links cannot fall behind the way
  the old project page did.

### Changed
- The CLAP plugin's saved state gained a short trailer for its own settings, after the
  engine's block. State written by an older version still loads, with the algorithm knob
  at Patch.

## 2.2.2 (2026-09-18)

### Fixed
- Windows standalone: the window was a bare title bar, which looked like a failed launch
  (theabolton/hexter#18 again, after 2.2.1 removed the dialog). That is how clap-wrapper shows a
  plugin with no GUI, with the settings hidden in the system menu behind the icon. The window
  now has a body that says the synth is running and where the menu is. The README says so too.

### Added
- Android: Bluetooth MIDI keyboards. A **Bluetooth MIDI** button scans for keyboards that
  advertise the BLE MIDI service, connects the one you pick through Android's MIDI service,
  and from then on it is an input like a USB keyboard. Android 12 and newer ask for the
  Bluetooth permission; older versions ask for location, which is how they gate the scan.
  Verified on the emulator up to the scan dialog; not yet tried against a real keyboard.

## 2.2.1 (2026-09-17)

### Fixed
- Windows standalone: "Unable to configure audio: RtApi::getDeviceInfo: deviceId argument
  not found" on a machine with no microphone, or whose default output failed to probe,
  reported against 2.1.2 on theabolton/hexter#18. 2.1.1 stopped the engine asking RtAudio
  about the missing device, but the Windows window still asked for its name and sample
  rates, and forced the input direction open, before the first window appeared. Every
  lookup is guarded now: a missing direction shows as "None" in Audio/MIDI Settings, is
  not opened, and the sample-rate list comes from the device that plays.

### Added
- An iPhone and iPad app in `ios/`, the Android app's twin: the engine behind an
  AVAudioEngine source node, every CoreMIDI source (USB or Bluetooth) feeding it, Apple's
  Bluetooth MIDI pairing screen, the bundled banks plus any bank file from Files, a voice
  list, volume, a level meter and a two-octave keyboard. iOS 16 or newer. Apple does not
  allow distributing it outside its store, so the README walks a first-time Xcode user
  through building it onto their own phone with a free Apple ID. CI builds it for the
  simulator and for a device.

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

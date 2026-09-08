# Changelog

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

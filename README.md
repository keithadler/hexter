# hexter

**A Yamaha DX7 modeling software synthesizer.** Six-operator FM, all 32
algorithms, the real envelope and LFO behaviour of the instrument, loading
any DX7 bank ever made. Free software since 2004, by Sean Bolton.

Version 2 is the same synth engine, revived for 2026: it now builds as a
**CLAP** and an **LV2** plugin on Linux, macOS and Windows, with CMake, tests
and CI, and comes with a command line renderer. The DSSI plugin and its GTK2
editor still build on Linux when their libraries are installed.

| | |
|---|---|
| Plugins | CLAP (`hexter.clap`), LV2 (`hexter.lv2`), Audio Unit (`hexter.component`, macOS), DSSI (legacy, Linux) |
| Platforms | Linux, macOS (Apple silicon and Intel), Windows |
| Banks | `.syx`, `.dx7`, `.mid` (sysex inside a MIDI file), `.tx7`, `.snd`, `.bnk`, `.dx2`, raw packed voices |
| Sysex | DX7 single voice, 32-voice bulk dump, voice and function parameter changes |
| License | GPL-2.0-or-later |

## Download

Builds for every platform are attached to each
[release](https://github.com/keithadler/hexter/releases). Unzip and copy:

| Platform | CLAP | LV2 | Audio Unit |
|---|---|---|---|
| Linux | `~/.clap/` | `~/.lv2/` | |
| macOS | `~/Library/Audio/Plug-Ins/CLAP/` | `~/Library/Audio/Plug-Ins/LV2/` | `~/Library/Audio/Plug-Ins/Components/` |
| Windows | `%COMMONPROGRAMFILES%\CLAP\` | `%APPDATA%\LV2\` | |

**Logic Pro and GarageBand** use the Audio Unit. After copying it, restart
Logic; it appears under AU Instruments as Keith Adler > hexter. It passes
Apple's `auval`, the check Logic runs before listing a plugin. The AU is the
CLAP wrapped by [clap-wrapper](https://github.com/free-audio/clap-wrapper),
so it has the same five parameters, and Logic saves the bank with the project.

The macOS build is unsigned. If macOS refuses to load it, remove the
quarantine flag once:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/hexter.clap
```

## Using it

hexter has no window of its own. Your host shows its five parameters and
you play it over MIDI, which is how a real DX7 module works:

| Parameter | Range | Notes |
|---|---|---|
| Program | 1 to 128 | Named from the loaded bank. MIDI program change works too. |
| Tuning | 415.3 to 466.2 Hz | A4 |
| Volume | -70 to +20 dB | |
| Polyphony | 1 to 64 voices | |
| Voice mode | Poly, Mono, Mono legato, Mono both | |

**Loading a bank.** In a CLAP host, use the host's preset browser and point
it at any bank file (hexter implements `clap.preset-load`). In an LV2 host,
set the *Bank file* property. Either way you can also send the bank as a
**DX7 bulk dump over MIDI**, exactly as you would to the hardware, or set
the environment variable `HEXTER_DEFAULT_BANK` to a file that loads on
every instance. Six banks ship in `banks/`, including the original DX7
ROM cartridges.

**Editing.** Send DX7 parameter-change sysex (from a hardware DX7, a
librarian, or an editor such as Dexed) and hexter follows, including
operator changes on notes that are already sounding. The edit buffer is saved
with your session.

**Controllers.** Mod wheel, breath, foot and aftertouch do what the
performance data of the patch says, as on the DX7. Pitch bend range,
portamento and controller assignments answer to DX7 function-parameter
sysex. Sustain, all-notes-off and all-sound-off are honoured.

## The command line renderer

`hexter-render` plays notes or a whole MIDI file through the engine and
writes a WAV. No host, no audio device.

```bash
hexter-render --bank banks/dx7_roms.dx7 --list
hexter-render --bank banks/dx7_roms.dx7 --program 11 --note 60 --note 64 --note 67 --out epiano.wav
hexter-render --bank banks/dx7_roms.dx7 --midi song.mid --out song.wav
```

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

That produces `build/hexter.clap`, `build/lv2/hexter.lv2/`, `build/hexter-render`,
and on macOS `build/auv2/hexter.component`.
The CLAP and LV2 headers are fetched by CMake if not installed. On Linux,
installing `dssi-dev liblo-dev libgtk2.0-dev libasound2-dev` also builds
the original DSSI plugin and GTK2 editor. Installing `lilv-dev` (or `brew
install lilv`) enables the LV2 host test.

Options: `-DHEXTER_BUILD_CLAP`, `-DHEXTER_BUILD_LV2`, `-DHEXTER_BUILD_DSSI`,
`-DHEXTER_BUILD_TOOLS`, `-DHEXTER_BUILD_TESTS` (all on),
`-DHEXTER_FLOATING_POINT` (off; the default is the fixed-point engine, which
renders identically on every machine).

## Embedding the engine

`src/hexter_engine.h` is a small C API with no plugin framework attached:
create an engine at a sample rate, feed it events with frame offsets,
render mono float. State save and load, bank loading, sysex and MIDI
parsing are all there. The CLAP and LV2 plugins and the renderer are each
a few hundred lines on top of it.

## What changed in 2.0

- CLAP and LV2 plugins, on three platforms, with per-platform CI and a
  `clap-validator` pass on every commit.
- CMake replaces autotools. DSSI, liblo, ALSA and GTK are no longer required.
- The engine is separated from the plugin API (`hexter_engine.h`) and covered
  by tests: bank loading in every supported format, sysex, rendering, voice
  management, state.
- Sysex over MIDI: bulk dumps, single voices, parameter changes. (Issues
  [#9](https://github.com/theabolton/hexter/issues/9),
  [#17](https://github.com/theabolton/hexter/issues/17))
- `HEXTER_DEFAULT_BANK` loads a bank on startup.
  ([#16](https://github.com/theabolton/hexter/pull/16))
- `hexter-render` renders MIDI files to WAV with no host, which is the
  standalone that was asked for.
  ([#18](https://github.com/theabolton/hexter/issues/18))
- A left shift of negative values in the fixed-point macros, undefined in C,
  is now a multiply.

Not yet: a graphical editor. The GTK2 editor still builds on Linux for DSSI
hosts; a toolkit-independent editor for the CLAP and LV2 plugins is the
obvious next piece of work.

## Credits

hexter was written by Sean Bolton, with contributions from Martin
Tarenskeen, Jamie Bullock, Rui Nuno Capela and others listed in
[AUTHORS](AUTHORS). It draws on Juan Linietsky's rx-saturno and Peter
Hanappe's FluidSynth. The 2.0 port is by Keith Adler.

The original README, with the history of the emulation and how the DX7
was reverse-engineered, is kept as [docs/README-1.1.rst](docs/README-1.1.rst).

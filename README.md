# hexter

[![CI](https://github.com/keithadler/hexter/actions/workflows/ci.yml/badge.svg)](https://github.com/keithadler/hexter/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/keithadler/hexter?sort=semver)](https://github.com/keithadler/hexter/releases/latest)
[![License](https://img.shields.io/github/license/keithadler/hexter)](https://github.com/keithadler/hexter/blob/master/COPYING)

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
| Standalone | `hexter.app` (macOS), `hexter` (Linux), `hexter.exe` (Windows): its own window, audio and MIDI, no host needed |
| Android | `hexter-<version>-android.apk`: a phone or tablet app with the same engine, USB MIDI in, and an on-screen keyboard |
| iPhone and iPad | the same app for iOS, in `ios/`; Apple does not allow a download, so you build it yourself with Xcode (see below) |
| Platforms | Linux (x86_64 and arm64), macOS (Apple silicon and Intel), Windows (x64 and arm64), Android (8.0 or newer, 64-bit), iOS (16 or newer, built with Xcode) |
| Banks | `.syx`, `.dx7`, `.mid` (sysex inside a MIDI file), `.tx7`, `.snd`, `.bnk`, `.dx2`, raw packed voices |
| Sysex | DX7 single voice, 32-voice bulk dump, voice and function parameter changes |
| License | GPL-2.0-or-later |

## Download

Builds for every platform are attached to each
[release](https://github.com/keithadler/hexter/releases). Unzip and copy:

| Platform | CLAP | LV2 | Audio Unit | Standalone |
|---|---|---|---|---|
| Linux | `~/.clap/` | `~/.lv2/` | | `hexter`, run it |
| macOS | `~/Library/Audio/Plug-Ins/CLAP/` | `~/Library/Audio/Plug-Ins/LV2/` | `~/Library/Audio/Plug-Ins/Components/` | `hexter.app`, anywhere |
| Windows | `%COMMONPROGRAMFILES%\CLAP\` | `%APPDATA%\LV2\` | | `hexter.exe`, anywhere |

**No DAW?** The standalone is hexter in a window of its own. It opens on the default audio
output, listens on every MIDI input it finds, and has an *Audio/MIDI Settings* panel for the
output device and sample rate. Plug in a keyboard and play. It has no editor, so choose voices
with MIDI program change, and load a bank by sending a DX7 bulk dump or by setting
`HEXTER_DEFAULT_BANK` before starting it (see below).

**On Windows the window is small on purpose.** hexter has no controls to show, so the window
is a short note saying it is running. *Audio/MIDI Settings*, and saving or loading its state,
are in the menu behind the icon at the top left of the window; right-clicking the title bar
opens the same menu. Versions before 2.2.2 showed only the title bar, which looked like a
failed launch and was not one.

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

## Android

hexter runs on an Android phone or tablet as an app: the same DX7 engine, a list of voices to
pick from, a two-octave keyboard on the screen, and MIDI in from a keyboard plugged into the
USB port. It is not in the Play Store. You install it from a file, which Android allows and
asks you to confirm once. Here is the whole procedure, for someone who has never done it:

1. **On the phone, open this page in the browser:**
   [github.com/keithadler/hexter/releases](https://github.com/keithadler/hexter/releases).
   Under the newest release, tap the file whose name ends in **`-android.apk`**. The browser
   downloads it; you may see a warning that this type of file can harm your device. That is
   the standard warning for any app that does not come from the store. Tap **Download anyway**
   or **OK**.
2. **Open the downloaded file.** Pull down the notification shade and tap the download, or
   open the **Files** app and look in **Downloads**.
3. **Allow the install.** The first time, Android says something like *"For your security,
   your phone is not allowed to install unknown apps from this source."* Tap **Settings**,
   turn on **Allow from this source**, and go back. Then tap **Install**. Android may scan the
   app with Play Protect first; let it.
4. **Open hexter.** It starts with the original DX7 ROM voices loaded. Tap a voice in the
   list and play the keyboard at the bottom of the screen. The orange bar next to the volume
   slider moves when sound is coming out.
5. **Plug in a keyboard.** Any keyboard or synth with USB MIDI, such as the M-VAVE FM-1,
   connects with a **USB OTG cable or adapter**: the small end goes in the phone, the
   keyboard's USB cable goes in the other end. The line under the bank name changes from
   *"No MIDI input"* to *"MIDI in: ..."* and the keyboard plays hexter. Turn the volume up
   on the phone; the keyboard's own volume does nothing here.
6. **Other banks.** The menu at the top holds the six banks the desktop versions ship. **Open
   bank…** loads any DX7 bank file on the phone, `.syx` or `.dx7`, for instance one you
   downloaded or received in a message. A bank sent from a keyboard as a bulk dump over MIDI
   loads too.

**Updating.** Download the new `.apk` and install it the same way. If Android refuses with
*"App not installed"* or a message about a different signature, uninstall hexter first (hold
its icon, tap **Uninstall**), then install the new one. Your own bank files are not inside the
app, so nothing is lost.

**What it needs.** Android 8.0 or newer on a 64-bit device, which is every phone sold in the
last several years. A USB keyboard needs no setup. For a Bluetooth MIDI keyboard, tap
**Bluetooth MIDI**, allow the permission Android asks for, and pick the keyboard from the list.

## iPhone and iPad

hexter runs on an iPhone or iPad with the same screen as the Android app: the voices of a
bank, volume, a level meter, a two-octave keyboard on the glass, and any MIDI keyboard over
USB or Bluetooth. There is no download for it. Apple does not let an iPhone install an app
from a file the way Android does; every app must be signed through Apple. The free way is
to build it yourself with Xcode on a Mac, which takes about twenty minutes the first time
and needs nothing but an Apple ID. These steps assume you have never opened Xcode.

1. **Install Xcode** from the Mac App Store. It is free and large. Open it once, let it
   finish installing its components, and when it asks which platforms to add, include iOS.
2. **Get the code.** On this page, click the green **Code** button, then **Download ZIP**,
   and unzip it. (If you know git, `git clone` works too.)
3. **Open the project.** In the unzipped folder, open `ios/Hexter.xcodeproj` by
   double-clicking it. Xcode opens with the project on the left.
4. **Sign in with your Apple ID.** In the Xcode menu choose **Settings**, then
   **Accounts**, click **+**, pick **Apple ID**, and sign in. Any Apple ID works. You do not
   need the paid developer program.
5. **Pick yourself as the team.** Click **Hexter** at the very top of the file list on the
   left, select the **Hexter** target, open the **Signing & Capabilities** tab, and under
   **Team** choose your name, shown as *(Personal Team)*. If Xcode says the bundle
   identifier is already in use, change `org.keithadler.hexter` on that tab to anything
   with your own name in it, such as `com.yourname.hexter`.
6. **Turn on Developer Mode on the phone.** On the iPhone, open **Settings**, then
   **Privacy & Security**, scroll to **Developer Mode**, turn it on, and restart when asked.
   (iOS 16 or newer. Older versions skip this step.)
7. **Plug the phone into the Mac** with a cable and unlock it. If the phone asks whether to
   trust this computer, tap **Trust**. In Xcode's toolbar, click the device name next to
   "Hexter" and choose your iPhone from the list.
8. **Press Run**, the ▶ button at the top left (or ⌘R). Xcode builds the app and copies it
   to the phone. The first time, the phone refuses to open it: on the phone go to
   **Settings**, **General**, **VPN & Device Management**, tap your Apple ID under
   *Developer App*, and tap **Trust**. Then tap the hexter icon on the home screen.

**The catch.** An app signed with a free Apple ID stops opening after 7 days. Plug the phone
in and press Run again, and it works for another 7. A paid developer account ($99 a year)
extends that to a year. Nothing else is different.

**No iPhone at hand?** Choose an iPhone simulator instead of your phone in step 7. The app
runs on the Mac's screen and plays through the Mac's speakers. The simulator does not pass
MIDI keyboards through, so use the keyboard on the screen.

**What it needs.** iOS 16 or newer. USB MIDI keyboards plug in through a Lightning or USB-C
adapter, no driver needed. For a Bluetooth MIDI keyboard, tap **Bluetooth MIDI** in the app
and pick it. To load your own banks, tap **Open bank…** and pick any `.dx7` or `.syx` file
from Files, or drop files into the hexter folder in Files under *On My iPhone*.

## Using it

hexter has no window of its own. Your host shows its six parameters and
you play it over MIDI, which is how a real DX7 module works:

| Parameter | Range | Notes |
|---|---|---|
| Program | 1 to 128 | Named from the loaded bank. MIDI program change works too. |
| Algorithm | Patch, or 1 to 32 | Forces the patch's operator wiring. See below. |
| Tuning | 415.3 to 466.2 Hz | A4 |
| Volume | -70 to +20 dB | |
| Polyphony | 1 to 64 voices | |
| Voice mode | Poly, Mono, Mono legato, Mono both | |

**The algorithm knob** is the one on the front panel of the hardware: it picks
which of the 32 operator wirings the patch uses, and turning it is the fastest
way to make a familiar patch into something else. It is an override rather than
a setting: at **Patch**, its resting position, every program plays its own
algorithm and the knob does nothing. Move it to 1-32 and that wiring is forced,
which you hear on notes you are already holding rather than only on the next
one. Move it back to Patch and the patch's own algorithm returns. Selecting a
program also brings the patch's algorithm back. It automates like any other
parameter.

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

On Windows, build with MSVC (the Visual Studio Build Tools) and `-G Ninja` to get the
standalone; clap-wrapper's Windows shell is C++/WinRT, which MinGW cannot compile. A MinGW
build still produces the plugins and the tool.

That produces `build/hexter.clap`, `build/lv2/hexter.lv2/`, `build/hexter-render`,
the standalone in `build/wrapped/`,
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

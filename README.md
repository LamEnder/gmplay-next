# gmplay

A video player for jailbroken Kindles with e-ink screens. It plays ordinary
video files (MP4, MKV, WebM, AVI, …) with sound over Bluetooth headphones,
and has a touch interface: a file browser and a tap-to-pause control overlay.

gmplay 2.0 builds on geekmaster's original **gmplay 1.5a** (2012), which played
pre-converted `.gmv` streams. The original source is kept in
`src/gmplay-1.5a.c`, and `.gmv` files still play.

Tested on a Kindle PaperWhite 4 (firmware 5.18.1.1.1). It targets the
`kindlehf` platform (firmware 5.16.3 and later).

---

## Contents

- [Preparing videos (transcoding)](#preparing-videos-transcoding)
- [Installing on the Kindle](#installing-on-the-kindle)
- [Using gmplay](#using-gmplay)
- [Command-line options](#command-line-options)
- [Supported formats](#supported-formats)
- [Performance](#performance)
- [Building from source](#building-from-source)
- [How it works](#how-it-works)
- [Troubleshooting](#troubleshooting)
- [License](#license)

---

## Preparing videos (transcoding)

**Transcode videos on your computer before copying them to the Kindle.** The
Kindle has one slow CPU core and no hardware video decoder, so a typical 720p
or 1080p download is too heavy: the picture falls behind the sound and looks
like slow motion.

### The easy way: `tools/kindle-transcode.sh`

Needs [FFmpeg](https://ffmpeg.org/) on your computer.

```sh
tools/kindle-transcode.sh my-video.mkv               # -> kindle-videos/my-video.mp4
tools/kindle-transcode.sh ~/Downloads/*.mp4          # several at once
tools/kindle-transcode.sh -c mpeg4 my-video.mkv      # lightest on the Kindle's CPU
```

| Option | Default | Effect |
|---|---|---|
| `-f FPS` | 12 | Frame rate. Sources that are already slower keep theirs. |
| `-H PIXELS` | 360 | Height. Smaller sources are not upscaled. |
| `-c CODEC` | `h264` | `h264`, or `mpeg4` (easiest to decode, files ~1/3 bigger). |
| `-q N` | 22 / 4 | Quality: x264 CRF for `h264`, qscale for `mpeg4`. Lower = better and bigger. |
| `-n` | | No audio. |
| `-o DIR` | `kindle-videos` | Output folder. |
| `-y` | | Overwrite existing outputs (otherwise they are skipped). |

Then copy the files from `kindle-videos/` into `videos/` on the Kindle.

### The command it runs

```sh
ffmpeg -i input.mp4 -vf "fps=12,scale=-2:360,format=gray,format=yuv420p" \
       -c:v libx264 -preset slow -crf 22 -tune fastdecode \
       -c:a aac -b:a 128k -ar 44100 output.mp4
```

| Part | Why |
|---|---|
| `fps=12` | **The biggest saving.** The screen shows at most ~11 frames/s (5 with plain refreshes), so higher frame rates are decoded only to be thrown away. A 60 fps source at 60 fps costs over 4× the CPU. |
| `scale=-2:360` | 360 pixels tall, width to match. 480p already costs ~65% more CPU; the screen dithers the picture anyway. |
| `format=gray,format=yuv420p` | Drops the color (the Kindle is grayscale) but keeps a standard 8-bit format. Saves a little decode time, and the bits go into detail instead of color. Also avoids 10-bit output, which some FFmpeg builds produce and which decodes much more slowly. |
| `-c:v libx264 -preset slow -crf 22` | H.264 at good quality and a small size. Raise `-crf` (e.g. 26) for smaller files. |
| `-tune fastdecode` | Turns off H.264 features that are expensive to decode (CABAC, deblocking, weighted prediction): ~40% less CPU per frame. |
| `-c:a aac -b:a 128k -ar 44100` | AAC audio at the Kindle mixer's rate, so gmplay doesn't have to resample. |

For the lightest load, replace the video codec options with `-c:v mpeg4 -q:v 4`
(MPEG-4 Part 2, "DivX"): about 35% less CPU than H.264, files about a third bigger.

### Measurements

Kindle PaperWhite 4, 30 s of a 1080p 60 fps trailer, every variant 640×360 with
the same audio. *CPU* is decoding time per second of video on the Kindle
(lower leaves more for drawing, audio and kdc). *Quality* is luma SSIM
against the source (1.0 = identical).

| Encoding | CPU (ms/s) | Size (30 s) | Quality |
|---|---|---|---|
| H.264 fastdecode, 60 fps | 417 | 7.3 MB | 0.961 |
| H.264 default (CABAC), 30 fps | 402 | 5.1 MB | 0.972 |
| H.264 fastdecode, 480p, 30 fps | 388 | 9.1 MB | 0.972 |
| H.264 fastdecode, 30 fps | 237 | 6.1 MB | 0.968 |
| H.264 fastdecode, 15 fps | 133 | 4.8 MB | 0.974 |
| **H.264 fastdecode, 12 fps, gray** (default) | **100** | **3.8 MB** | **0.978** |
| **MPEG-4, 12 fps, gray** (`-c mpeg4`) | **65** | 5.1 MB | 0.968 |
| MPEG-2, 30 fps | 139 | 12.6 MB | 0.968 |
| MJPEG, 30 fps | 159 | 18.8 MB | 0.971 |
| VP8, 30 fps | 242 | 3.9 MB | 0.940 |

Played in gmplay with overdrive on: the 60 fps file showed **1.8 frames/s** and
fell behind the sound; both 12 fps files showed **12.0 frames/s with no frames
dropped**.

### Variations

- **Overdrive off / plain refreshes (~5 frames/s on screen):** `-f 8` saves
  even more.
- **Slightly sharper:** `-H 480`, if `gmplay -v` shows the decoder keeping up
  (see [Performance](#performance)).
- **No audio needed:** `-n` for a smaller file.

---

## Installing on the Kindle

You need a jailbroken Kindle on firmware 5.16.3 or later (`kindlehf`).

### From a release (easiest)

1. Download `gmplay-next-<version>-kindlehf.zip` from the
   [Releases page](https://github.com/LamEnder/gmplay-next/releases).
2. Connect the Kindle over USB and unzip the file into the root of the Kindle
   drive. This creates `gmplay/`, `documents/gmplay.sh` and an empty `videos/`
   folder.
3. Eject the Kindle. **gmplay** now appears in the library.
4. Copy [transcoded](#preparing-videos-transcoding) videos into `videos/`.

### Manually

Paths are as seen on the Kindle; over USB the `/mnt/us/` folder is the root of
the Kindle drive.

| File | Goes to |
|---|---|
| `gmplay` (the built binary) | `/mnt/us/gmplay/gmplay` |
| `kindle/gmplay.sh` (scriptlet launcher) | `/mnt/us/documents/gmplay.sh` |
| your videos | `/mnt/us/videos/` |

The scriptlet appears in the Kindle library as **gmplay**. Opening it starts
the file browser. It creates `/mnt/us/videos/` if it doesn't exist and writes
gmplay's output to `/mnt/us/gmplay/gmplay.log`.

To change the default options, edit the `OPTS=""` line in `gmplay.sh`, for
example `OPTS="-o"` to turn on overdrive.

With SSH access (e.g. USBNetwork) you can also copy files with `scp -O` and run
`/mnt/us/gmplay/gmplay` directly.

---

## Using gmplay

### File browser

Lists the videos in `/mnt/us/videos/`, sorted by name, with their sizes.

- **Tap a video** to play it. Playback continues with the next video in the
  list when one finishes.
- **Trash icon** on a row: deletes that video after a confirmation dialog
  (**Cancel** / **Delete**). The file is removed from the Kindle.
- **◀ / ▶** at the bottom change page when there are more videos than fit.
- **X** (top right) exits gmplay and returns to where you were (Kindle home
  screen or KOReader).

### While playing

Landscape videos are rotated to fill the screen. Hold the Kindle sideways.

**Tap anywhere** to pause and show the controls:

| Control | Action |
|---|---|
| **X** (top right) | Close the video, back to the browser |
| **OD: off / on / max** (top) | Cycle the overdrive mode (see [Performance](#performance)) |
| Seek bar | Tap anywhere on it to jump to that point |
| **⏮ / ⏭** | Previous / next video in the list |
| **-10s / +10s** | Skip back / forward 10 seconds |
| **▶**, or tap the picture | Resume |

Sound stops while paused and resumes from the frame on screen.

### Sound

Sound goes through the Kindle's own audio system, which on most models means
**Bluetooth headphones or speakers**. Pair them in the Kindle's settings
first. Without a connected audio device, gmplay plays video only.

If sound and picture are out of sync, adjust the delay with `-a`: increase it
(e.g. `-a 250`) if the sound comes after the picture, decrease it if the sound
comes first.

### Other apps

While gmplay runs, it pauses the Kindle interface and KOReader so they can't
draw over the video, and keeps the screensaver from starting. When it exits, it
restores the screen exactly as it was and resumes them.

---

## Command-line options

```
gmplay [options] [folder | file...]
```

With a folder (default `/mnt/us/videos`), gmplay shows the file browser. With
files, it plays them in order and exits. `-` reads a `.gmv` stream from stdin.

| Option | Effect |
|---|---|
| `-r auto\|on\|off` | Rotate landscape video 90° to fill a portrait screen. Default `auto`. |
| `-g` | 16-level grayscale video instead of black and white. Slower screen refresh. |
| `-c SECS` | Full flashing refresh every SECS seconds to clear ghosting. Off by default. |
| `-o` | Overdrive "on": capped at 10 fps. |
| `-O` | Overdrive "max": uncapped. |
| `-f FPS` | Cap frames shown per second. Overrides the 10 fps cap of `-o`. |
| `-m` | Mute. |
| `-a MS` | Audio output delay to compensate for (Bluetooth). Default 150. |
| `-S` | Use the slower generic scaler (for troubleshooting). |
| `-K PRESET` | Play video through [kdc](#kdc-per-pixel-drive) with a calibrated preset (`.doses`). Needs the pulse waveform installed; falls back to FBInk otherwise. |
| `-T` | Touch test: draws a dot wherever you tap. |
| `-v` | Verbose: device info, file info and timing statistics. |

Environment variables:

| Variable | Effect |
|---|---|
| `GMPLAY_TOUCH` | Fix touch mapping if taps land in the wrong place: any of `swap`, `mirror_x`, `mirror_y`, comma separated. Check with `-T`. |
| `GMPLAY_FREEZE` | Processes to pause while playing, space separated. Default `cvm awesome reader.lua` (Kindle UI, window manager, KOReader). |

---

## Supported formats

| | |
|---|---|
| **Containers** | MP4 / MOV / M4V / 3GP, MKV / WebM, AVI, MPEG-TS, MPEG-PS (`.mpg`), FLV, Ogg (`.ogv`), WMV / ASF, legacy `.gmv` |
| **Video** | H.264, H.265 (HEVC), MPEG-4 Part 2 (DivX/Xvid), H.263, MPEG-1/2, VP8, VP9, MJPEG, Theora, WMV 1/2/3, VC-1, Sorenson (FLV) |
| **Audio** | AAC, MP3, MP2, Opus, Vorbis, FLAC, ALAC, AC-3 / E-AC-3, WMA v1/v2, PCM |
| **Not supported** | AV1, DTS, WMA Pro/Lossless, ProRes, RealMedia |

The browser lists `.mp4 .m4v .mov .3gp .mkv .webm .avi .ts .mpg .mpeg .flv
.ogv .wmv .asf .gmv`. Other files can still be played from the command line.

H.265 and VP9 decode much more slowly than H.264 on the Kindle's CPU.
[Transcode](#preparing-videos-transcoding) them to H.264.

---

## Performance

E-ink screens are slow: a full-screen black-and-white refresh takes around
130–170 ms on a PaperWhite 4. gmplay drops video frames to keep up with the
sound; it never slows the sound down.

### Overdrive

| Mode | How | Frame rate (PW4, 360p) | Glitches |
|---|---|---|---|
| **off** (default) | Waits for each refresh to finish before starting the next | ~5 fps | none |
| **on** (`-o`) | Starts the next refresh while the previous one is still finishing, capped at 10 fps | ~10 fps | occasional |
| **max** (`-O`) | Same, uncapped | ~18–21 fps | frequent |

Glitches happen where a refresh is overtaken by the next one. Pick a different
cap with `-o -f N`.

### kdc (per-pixel drive)

With `-K`, video goes through **kdc**, a per-pixel pulse-drive controller
([kindle-kdc](https://github.com/LamEnder/kindle-kdc)). It doesn't refresh whole frames
with A2: a custom short "pulse" waveform drives only the pixels that still
need to change, up to ~10 times a second, and it never makes gmplay wait.
On a PW4 (Bad Apple, 30 s):

| | Panel updates | New frames/s | Contrast / ghosting |
|---|---|---|---|
| FBInk A2 | 136 | 4.6 | baseline |
| kdc, `pw4_install3_video.doses` | 303 | **10.9** | better contrast, less ghosting, smoother motion |

To use it, set `KDC_PRESET` in `gmplay.sh` (e.g. `pw4_install3_video.doses`),
with the kdc tools and presets in `/mnt/us/gmplay/kdc/` (`kdc-pulse.sh`,
`kdc-session.sh`, `kdc-wfm`, `*.doses`). The launcher pauses the Kindle UI,
installs the pulse waveform, plays, restores the stock waveform and resumes
the UI. The pause overlay still uses FBInk: kdc is closed while it shows.

### Checking a file

Run a video with `-v` over SSH. At the end it prints timing statistics:

```
gmplay: decoder too slow, skipping non-reference frames
gmplay: avg ms: decode 72.5/frame, draw 59.5 + panel wait/refresh 45.7 per shown frame
french_cancan.mp4: 36 frames shown, 231 dropped, 1.6 fps
```

If **decode** is above about 70–80% of the frame time (1000 ÷ the video's
frame rate: 42 ms at 24 fps, 33 ms at 30 fps), or you see **decoder too slow**,
the file is too heavy: [transcode it](#preparing-videos-transcoding) with `tools/kindle-transcode.sh`.
The example above is a 720p file (72.5 ms against a 42 ms budget). A 360p file
decodes in under 10 ms per frame.

---

## Building from source

Requirements, on a Linux build machine:

- [koxtoolchain](https://github.com/koreader/koxtoolchain) for
  `arm-kindlehf-linux-gnueabihf` (in `~/x-tools/`)
- [kindle-sdk](https://github.com/KindleModding/kindle-sdk), set up with
  `./gen-sdk.sh kindlehf`
- `meson`, `ninja`, `make`, `curl`, `git`, `pkg-config`

```sh
# 1. Cross-compile FFmpeg 7.1.1 and FBInk v1.25.0 as static libraries into
#    deps/kindlehf/ (first run takes a few minutes)
scripts/build-deps.sh

# 2. Configure and build
meson setup build-kindlehf --cross-file ~/x-tools/arm-kindlehf-linux-gnueabihf/meson-crosscompile.txt
ninja -C build-kindlehf

# 3. Strip for the Kindle (optional, ~5 MB)
~/x-tools/arm-kindlehf-linux-gnueabihf/bin/arm-kindlehf-linux-gnueabihf-strip \
    -o build-kindlehf/gmplay.stripped build-kindlehf/gmplay
```

The binary is statically linked against FFmpeg and FBInk. On the Kindle it only
needs `libc`, `libm`, `libpthread` and `libz`, which every firmware has.

Optional kdc output (`-K`): clone [kindle-kdc](https://github.com/LamEnder/kindle-kdc)
and build it first (`make kindle` in its directory),
then point meson at it:

```sh
meson configure build-kindlehf -Dkdc_dir=/path/to/kindle-kdc
ninja -C build-kindlehf
```

Without `kdc_dir`, gmplay builds exactly as before, and `-K` just reports that
kdc isn't built in.

### Tests

```sh
tests/run-host-tests.sh
```

Runs on the build machine (native `gcc`, with sanitizers). It checks the fast
scaler against a reference implementation. Run it after changing
`src/scale.c`.

### Font

UI text uses DejaVu Sans, rendered into `src/font_data.c` by
`tools/gen_font.py` (needs Pillow: `apt install python3-pil`). The generated
file is committed, so normal builds don't need Pillow.

---

## How it works

| File | Role |
|---|---|
| `src/main.c` | Options, startup and shutdown, browser / playlist loop |
| `src/player.c` | Decoding, frame timing, pause overlay, seeking |
| `src/audio.c` | Audio thread and output |
| `src/scale.c` | Fast scale + dither + rotate into the framebuffer |
| `src/screen.c` | Framebuffer, e-ink refreshes (via FBInk), canvas rotation |
| `src/browser.c` | File browser and delete dialog |
| `src/input.c` | Touchscreen taps |
| `src/ui.c` | Text, buttons and icons |
| `src/sys.c` | Pausing the Kindle UI / KOReader, screensaver |

- **Screen:** [FBInk](https://github.com/NiLuJe/FBInk) sends refreshes in the
  right form for each Kindle generation (i.MX and MediaTek). Video uses the fast
  A2 black-and-white mode with ordered dithering. The UI uses 16-level gray.
- **Drawing:** the video's luma plane is scaled, dithered and rotated in one
  NEON-vectorized pass, into a back buffer while the screen is still busy with
  the previous frame, then copied into the framebuffer.
- **Overdrive** waits only until the display controller has taken in the
  previous update (`MXCFB_WAIT_FOR_UPDATE_SUBMISSION`), not until it has
  finished, so frames are never half-written.
- **Audio:** the Kindle has no ALSA device for Bluetooth; its audio goes
  through Amazon's mixer (`audiomgrd`). gmplay decodes audio on its own thread
  and pipes 44.1 kHz stereo PCM into `gst-launch-0.10 fdsrc ! mixersink`, the
  streaming client the firmware ships. Video timing follows the audio position.

---

## Troubleshooting

| Problem | Fix |
|---|---|
| Video lags behind the sound / looks like slow motion | The file is too heavy to decode. Convert it with [`tools/kindle-transcode.sh`](#preparing-videos-transcoding) (12 fps, 360p). Check with `-v`. |
| No sound | Connect Bluetooth headphones in the Kindle settings before starting gmplay. `-v` prints "no audio output connected" if none is found. |
| Sound and picture out of sync | Adjust `-a` (default 150 ms). |
| Ghosting (faint traces of earlier frames) | Use `-c 30` for a cleaning refresh every 30 seconds. |
| Glitches with overdrive | Use `-o` instead of `-O`, or a lower cap such as `-o -f 6`. |
| Taps land in the wrong place | Run `gmplay -T`, then set `GMPLAY_TOUCH` (see [options](#command-line-options)). |
| A video doesn't appear in the browser | Check its extension is in the [supported list](#supported-formats), and that it is in `/mnt/us/videos/`. |
| "Video codec not supported" | The file uses a codec gmplay doesn't include (e.g. AV1). Transcode it. |

Logs: `/mnt/us/gmplay/gmplay.log` when started from the scriptlet.

---

## License

The gmplay source is under the MIT license (see [`LICENSE`](LICENSE)):
gmplay 1.5a is Copyright (C) 2012 geekmaster, gmplay 2.0 is Copyright (C) 2026
LamEnder.

The built binary statically links:

- **FBInk** — GPL-3.0. A distributed gmplay binary is therefore under GPL-3.0.
- **FFmpeg** — LGPL-2.1 or later (this build uses no GPL-only parts).
- The UI font, **DejaVu Sans**, is under the Bitstream Vera license.

# SANDWAVE - Cymatic Voice Plate

An audio effect plugin (VST3 / AU) by Uranium Music. It listens to the track's input (a microphone,
a voice, any sound) and shows it as sand on a vibrating Chladni plate: the dominant frequency picks
the plate's vibration mode and the loudness shakes the sand, which settles on the still nodal lines.
Audio passes through untouched.

## Controls

| Control | What it does |
|---|---|
| GAIN | Input gain before the analysis, -30..+30 dB |
| SENS | How quiet a sound still moves the sand |
| GRAIN | How far the grains jump |
| DECAY | How long the plate keeps ringing after the sound stops (50 ms .. 2 s) |
| RANGE | How many octaves the plate's 14 modes cover, starting at 60 Hz |
| FREQ | Manual mode: the plate rings at this point as if driven by a tone generator |
| MIC IN | On: follow the input. Off: manual mode (turning FREQ switches it off) |
| SQUARE / CIRCLE | Plate shape |
| SHAKE | Scatter the sand evenly again |

Click the header to see the version and license status.

## License

Same scheme as PSY-101: on first use the plugin asks for the e-mail and `URAN-XXXX-XXXX-XXXX-XXXX` key
and checks them with `POST /api/verify` on the Uranium Music license server; every later load checks
`GET /lisencecheck/<md5(key + machineId + UTC date + "1.618")>` with `vstName=SANDWAVE`. Answers are
Ed25519-signed and verified against the public key compiled into `Source/License.cpp`; the last good
answer allows 72 hours offline. Until the license is verified the plate is locked; audio still passes.

The license server must list `SANDWAVE` in `VST_NAMES` for keys to be issued and accepted.

## Build

```bash
git submodule update --init
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j 8
```

`-DSANDWAVE_DEV_UNLOCKED=ON` builds a local test version without the license check. Never ship it.

Tests and screenshots without a DAW:

```bash
cmake --build build --target SandwaveSnapshot
./build/SandwaveSnapshot_artefacts/SandwaveSnapshot shots              # analysis tests + editor PNGs
./build/SandwaveSnapshot_artefacts/SandwaveSnapshot --license-test work  # license tests
```

Installers: `packaging/mac/build_dmg.sh` (.dmg with a .pkg) and `packaging/windows/installer.iss`
(Inno Setup); the `Installers` GitHub Action builds both.

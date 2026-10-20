[English](README.md) | [简体中文](README.zh_CN.md)

# Magpie Usage Display for AI Passport

ESP-IDF 5.5.3 firmware for the FoloToy AI Passport (ESP32-C3, 8 MB flash,
240×320 ST7789 display, no touch or PSRAM). It uses the existing repository's
macOS Companion and BLE v4 UUIDs without changing the Python service.
The original Waveshare PlatformIO firmware remains under `src/`.

## Features and controls

Four purpose-built portrait pages show Overview (success, calls, errors, quota),
Requests (model, HTTP status, latency, TTFT, tokens), Sessions (30-day sessions,
tokens, estimated cost, top model), and Context (cache hit and token components).
The header shows connection and battery status; missing battery readings show
`--%`. Model names use printable ASCII, replacing unsupported bytes with `?`.
Context uses Magpie's public API aggregates rather than its private app score.

| Input | Behavior |
| --- | --- |
| UP / DOWN click | Previous / next page, wrapping across four pages |
| OK click | Request immediate refresh |
| OK hold (BSP default 500 ms) | Open / close quick actions |
| UP / DOWN in actions | Select Refresh, Focus Magpie, Clear pairing, Back |
| OK in actions | Execute selection |
| Clear pairing confirmation | Release, then hold OK again; any other action cancels |
| Any key while backlight is off | Wake only; the whole initial gesture is consumed |

After 60 seconds without input, only the backlight turns off. BLE and metrics
continue updating; this is not deep sleep. Pairing keeps the screen awake.
Companion normally refreshes every 15 seconds; data becomes STALE at 60 seconds.
A connection without a valid snapshot for 90 seconds is disconnected so the
Companion can reconnect. Until a new connection's first snapshot, metrics are
hidden to avoid showing old data as current.

## Build and run

Install/activate **ESP-IDF v5.5.3** using the official
[ESP-IDF setup guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/get-started/index.html).
From this directory:

```bash
source /path/to/esp-idf/export.sh
./tools/validate.sh
```

The gate runs the firmware's C parser/state tests with ASan/UBSan, checks wire
compatibility against the production Python serializer, runs all Companion
tests, builds using isolated configuration, merges images and verifies their
layout and debug-artifact identity. Individual stages:

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
```

The resulting complete image is `build/FoloToy-AI-Passport-full.bin`, intended
for offset **0x0**. Matching ELF, MAP, individual images and SHA-256 manifest
are retained in `build/firmware/<full-image-sha256>/`. Verify an archive with:

```bash
python3 tools/archive_firmware.py verify build/firmware/<full-image-sha256>
```

For an authorized full firmware replacement, after confirming the serial port:

```bash
python -m esptool --chip esp32c3 --port /dev/cu.usbmodemXXXX \
  write_flash 0x0 build/FoloToy-AI-Passport-full.bin
```

The merged image replaces firmware and its padded gaps can reset stored NVS,
including old pairing records. No full-chip erase or firmware readback is
required. To preserve compatible NVS, use an incremental build and segmented
`idf.py -p PORT flash` after checking partition compatibility. Never flash the
application-only `FoloToy-AI-Passport.bin` at 0x0.

From the repository root, check Magpie, then start the existing Companion:

```bash
./companion/run.sh --once
./companion/run.sh
```

On first connection enter the six-digit display code into macOS's Bluetooth
pairing prompt. Encrypted, authenticated LE Secure Connections and NVS bonds
are enabled. Subsequent connections reuse bonds. If one side loses its keys,
clear pairing through the device actions and remove the old macOS pairing
before pairing again. Clearing bonds touches NimBLE's store, not unrelated NVS.
Only `refresh` and `focus_magpie` commands are supported; no credentials,
request bodies or arbitrary shell commands are sent to the board.

Both board variants advertise the same service/name for Companion compatibility.
Power on one display at a time when running the existing single-device Companion.

## Hardware acceptance

A successful build does not establish device compatibility. Check on an actual
AI Passport:

1. Correct colors, orientation, rounded corners, logo and all text within 240×320.
2. UP/DOWN/OK recognition, wraparound, action selection, cancellation and reset.
3. First pairing, displayed passkey, reconnect after device/Mac restarts, and bond reset.
4. Each metric against `./companion/run.sh --once`; requests and action results update.
5. STALE after stopping updates, automatic reconnect, OFFLINE on disconnection.
6. 60-second backlight timeout; every wake gesture has no secondary action.
7. Battery/missing-gauge behavior, no watchdog resets, and stable memory over prolonged use.

For host-rendered layout previews (not hardware validation), after the first IDF
build has downloaded LVGL, with IDF activated:

```bash
cmake -S tests/render -B build/render -G Ninja \
  -DLVGL_PATH="$PWD/managed_components/lvgl__lvgl"
cmake --build build/render
mkdir -p build/previews
./build/render/render "$PWD/build/previews"
```

This renders the actual UI into PPM files using LVGL 9.5 and the 24 KB LVGL
memory pool; it does not model SPI timing, BLE or physical button voltages.

## Source provenance

`components/bsp/`, `tools/archive_firmware.py`, `tools/verify_firmware.py`, base
configuration, partition table and dependency lock originate from
[FoloToy/ai-passport at 3d685a9](https://github.com/FoloToy/ai-passport/tree/3d685a90bdb96dd8526d84edde1e63fec25b460e).
The BSP is vendored unchanged; hardware mapping stays in `bsp_pins.h`. No demo
menu or demo pages are linked. `main/` contains this application's state,
NimBLE transport and LVGL UI. The small logo reuses this repository's bitmap
with an LVGL 9 descriptor. Upstream MIT terms are retained in `LICENSE`; see
also the repository's third-party notices. Dependencies stay pinned by
`dependencies.lock` and are fetched by the ESP-IDF component manager.

## Device test record — 2026-10-21

The user authorized a complete refresh of `/dev/cu.usbmodem1101`, including
resetting settings and Bluetooth bonds. The verified merged image SHA-256 was
`ce6d27697b1576dd1c06f3d4237e3683d8f92c97789302565dd779eba1ff192a`;
its matching ELF SHA-256 was
`daa97644cb928f8d143f5a64bb3cff3221c468b2970bdd29b5be7a613f08807a`.

- Flash write and esptool hash verification: PASS; ESP32-C3, 8 MB confirmed.
- Initial 25-second startup observation: PASS. The expected ELF identity and
  ESP-IDF 5.5.3 booted; display/LVGL, CW2017, ADC buttons and BLE advertising
  initialized without an observed crash or watchdog reset.
- Display and basic page/button interaction: user reported OK.
- Magpie local HTTP endpoint: reachable (HTTP 200).
- BLE pairing/data/actions/reconnection: NOT VERIFIED. Mac scanning reported
  Bluetooth unavailable. A subsequent serial observation showed a different
  application (`nabo-table-duel-offline`, ESP-IDF 6.0.2, different partitions).
  The user chose to retain the replacement firmware and stop testing. No
  further device operations were performed; the device is not assumed to
  still run this Magpie image.
- Backlight timeout/wake, prolonged memory stability and metric accuracy:
  still require acceptance with the intended firmware running.

Serial observation and the temporary Companion test were stopped, releasing
the port. No full-chip erase, commit, push or publication was performed.

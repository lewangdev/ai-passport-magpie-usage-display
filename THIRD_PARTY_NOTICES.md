# Third-party notices

This repository contains a generated bitmap subset of Montserrat Medium in
`src/fonts/font_montserrat_100.c`. Montserrat is Copyright 2024 The
Montserrat.Git Project Authors and is licensed under the SIL Open Font
License 1.1. See [`LICENSES/OFL-1.1.txt`](LICENSES/OFL-1.1.txt).

The project downloads but does not vendor the following runtime and build
dependencies:

| Dependency | License |
|---|---|
| ArduinoJson | MIT |
| LVGL | MIT |
| Arduino GFX | BSD 2-Clause |
| SensorLib | MIT |
| XPowersLib | MIT |
| Arduino-ESP32 | LGPL-2.1 |
| Bleak | MIT |
| pioarduino platform-espressif32 | Apache-2.0 |
| PlatformIO Core | Apache-2.0 |

Each dependency remains subject to its own copyright notices and license
terms. Refer to the dependency versions in `platformio.ini`,
`companion/pyproject.toml`, and `companion/requirements.txt`.

The AI Passport target vendors FoloToy's MIT-licensed BSP and firmware
verification/archive tools from commit `3d685a90bdb96dd8526d84edde1e63fec25b460e`.
Copyright (c) 2026 FoloToy; its license is retained at
[`firmware/ai-passport/LICENSE`](firmware/ai-passport/LICENSE).
This target additionally downloads ESP-IDF (Apache-2.0), esp_lvgl_port
(Apache-2.0), button (Apache-2.0), and esp_codec_dev (Apache-2.0), with versions
in `firmware/ai-passport/dependencies.lock`. Magpie's existing bitmap and
trademark attribution also applies to the LVGL 9 logo descriptor.

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mode="${1:---all}"
static_checks() { python3 tools/test_host.py; }
firmware_checks() (
    [[ "$(idf.py --version)" == "ESP-IDF v5.5.3" ]] || { echo "Activate ESP-IDF v5.5.3" >&2; exit 1; }
    validation_build="$(mktemp -d /tmp/magpie-passport-build.XXXXXX)"
    trap 'rm -rf -- "${validation_build}"' EXIT
    idf.py -B "${validation_build}" -D IDF_TARGET=esp32c3 \
        -D "SDKCONFIG_DEFAULTS=$PWD/sdkconfig.defaults" \
        -D "SDKCONFIG=${validation_build}/sdkconfig" build
    idf.py -B "${validation_build}" merge-bin -o "${validation_build}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build}"
    python3 tools/archive_firmware.py create "${validation_build}" --archive-root "$PWD/build/firmware"
    mkdir -p build
    cp "${validation_build}/FoloToy-AI-Passport-full.bin" build/FoloToy-AI-Passport-full.bin
    echo "Build: PASS"
)
case "$mode" in
    --static) static_checks ;;
    --firmware) firmware_checks ;;
    --all) static_checks; firmware_checks ;;
    *) echo "Usage: $0 [--static|--firmware|--all]" >&2; exit 2 ;;
esac

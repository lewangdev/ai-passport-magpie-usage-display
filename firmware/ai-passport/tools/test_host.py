#!/usr/bin/env python3
"""Test actual firmware protocol/reducer plus payloads from the existing Companion."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
sys.path.insert(0, str(REPO))
from companion.codex_display.metrics import build_snapshot
from companion.codex_display.protocol import encode_snapshot

idf_path = os.environ.get("IDF_PATH")
if not idf_path:
    sys.exit("Activate ESP-IDF 5.5.3 (IDF_PATH) to use its pinned cJSON sources for host tests.")
cjson = Path(idf_path) / "components/json/cJSON"
with tempfile.TemporaryDirectory(prefix="magpie-host-") as tmp:
    binary = Path(tmp) / "test_model"
    # Keep warnings strict for our code; Apple's SDK deprecates sprintf used by upstream cJSON.
    cjson_object = Path(tmp) / "cJSON.o"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wno-deprecated-declarations",
                    "-fsanitize=address,undefined", "-g", "-c", str(cjson / "cJSON.c"),
                    "-o", str(cjson_object)], check=True)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-g", "-I" + str(ROOT / "main"), "-I" + str(cjson),
                    str(ROOT / "tests/test_model.c"), str(ROOT / "main/magpie_model.c"),
                    str(cjson_object), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    # Use the production serializer, exercising its real wire representation.
    snapshot = build_snapshot({}, {}, now=1800000000, utc_offset_minutes=480)
    payload = encode_snapshot(snapshot, 42)
    result = subprocess.run([str(binary), "parse"], input=payload, capture_output=True, check=True)
    assert result.stdout.decode().split()[:4] == ["42", str(snapshot.tokens_today), str(snapshot.remaining_percent), str(snapshot.total_cost_micros)]
    valid = json.loads(payload)
    cases = [dict(valid, v=3), dict(valid, s=-1), dict(valid, s=1.2), dict(valid, r=101),
             dict(valid, o=9999), dict(valid, y=1001), dict(valid, d="bad"), dict(valid, m=42),
             dict(valid, x=[]), dict(valid, f=4294967296), dict(valid, b=2)]
    for index in (0, 7, 8, 9):
        x = list(valid["x"]); x[index] = "bad" if index != 8 else 123
        cases.append(dict(valid, x=x))
    for case in cases:
        raw = json.dumps(case, separators=(",", ":")).encode()
        assert subprocess.run([str(binary), "parse"], input=raw, capture_output=True).returncode == 2, case
    subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "companion/tests", "-v"], cwd=REPO, check=True)
print("Companion / firmware interoperability: PASS")

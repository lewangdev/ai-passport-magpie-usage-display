#include "magpie_model.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool number(const cJSON *item, double max)
{
    return cJSON_IsNumber(item) && isfinite(item->valuedouble) &&
           item->valuedouble >= 0 && item->valuedouble <= max &&
           floor(item->valuedouble) == item->valuedouble;
}
static uint64_t get(const cJSON *root, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return number(item, 9007199254740991.0) ? (uint64_t)item->valuedouble : 0;
}
static void text(const cJSON *item, char *out, size_t size)
{
    // The bundled font covers printable ASCII. Replace unsupported bytes visibly.
    const unsigned char *src = (const unsigned char *)(cJSON_IsString(item) ? item->valuestring : "-");
    size_t n = 0;
    while (*src && n + 1 < size) { out[n++] = *src >= 32 && *src < 127 ? *src : '?'; ++src; }
    out[n] = 0;
}
bool magpie_json_frame(const char *json, size_t length)
{
    if (!json || !length || length > MAGPIE_PAYLOAD_MAX || memchr(json, 0, length)) return false;
    // cJSON is recursive. Bound nesting before parsing on the 4 KB BLE host stack.
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        char ch = json[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
        } else if (ch == '"') quoted = true;
        else if (ch == '{' || ch == '[') { if (++depth > 4) return false; }
        else if (ch == '}' || ch == ']') { if (!depth) return false; --depth; }
    }
    return !quoted && depth == 0;
}
bool magpie_parse(const char *json, size_t length, uint32_t previous, magpie_snapshot_t *out)
{
    if (!out || !magpie_json_frame(json, length)) return false;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, length, &end, false);
    if (!root) return false;
    while (end < json + length && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) ++end;
    const cJSON *seq = cJSON_GetObjectItemCaseSensitive(root, "s");
    bool ok = end == json + length && cJSON_IsObject(root) && get(root, "v") == 4 &&
              number(seq, UINT32_MAX) && seq->valuedouble > 0 &&
              (previous == 0 || seq->valuedouble >= previous);
    // Require the fields emitted by Companion, avoiding a malformed "online zero" snapshot.
    const char *required[] = {"t", "r", "d", "f", "z", "y", "l", "h", "n", "b"};
    for (size_t i = 0; i < sizeof(required)/sizeof(required[0]); ++i)
        ok = ok && number(cJSON_GetObjectItemCaseSensitive(root, required[i]), 9007199254740991.0);
    const cJSON *offset = cJSON_GetObjectItemCaseSensitive(root, "o");
    ok = ok && cJSON_IsNumber(offset) && offset->valuedouble >= -840 && offset->valuedouble <= 840 &&
         floor(offset->valuedouble) == offset->valuedouble;
    const cJSON *x = cJSON_GetObjectItemCaseSensitive(root, "x");
    ok = ok && cJSON_IsArray(x) && cJSON_GetArraySize(x) == 10 &&
         cJSON_IsString(cJSON_GetArrayItem(x, 8)) && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(root, "m"));
    for (int i = 0; i < 10; ++i) if (i != 8)
        ok = ok && number(cJSON_GetArrayItem(x, i), 9007199254740991.0);
    const char *bounded[] = {"f", "z", "l", "h", "n"};
    for (size_t i = 0; i < sizeof(bounded)/sizeof(bounded[0]); ++i)
        ok = ok && get(root, bounded[i]) <= UINT32_MAX;
    ok = ok && get(root, "r") <= 100 && get(root, "y") <= 1000 && get(root, "b") <= 1;
    if (ok) {
        magpie_snapshot_t value = {0};
        value.sequence = (uint32_t)get(root, "s");
        value.unix_time = get(root, "t"); value.utc_offset = offset->valueint;
        value.remaining = get(root, "r"); value.tokens_today = get(root, "d");
        value.calls = get(root, "f"); value.errors = get(root, "z"); value.success = get(root, "y");
        value.ttft = get(root, "l"); value.status = get(root, "h"); value.latency = get(root, "n");
        value.failed = get(root, "b") != 0;
        text(cJSON_GetObjectItemCaseSensitive(root, "m"), value.latest_model, sizeof(value.latest_model));
        uint64_t *dest[] = {&value.sessions, &value.total_calls, &value.input, &value.output,
                            &value.cache, &value.reasoning, &value.cost_micros};
        for (int i = 0; i < 7; ++i) *dest[i] = (uint64_t)cJSON_GetArrayItem(x, i)->valuedouble;
        double hit = cJSON_GetArrayItem(x, 7)->valuedouble;
        double calls = cJSON_GetArrayItem(x, 9)->valuedouble;
        ok = hit <= 1000 && calls <= UINT32_MAX;
        value.cache_hit = hit <= 1000 ? (unsigned)hit : 0;
        value.top_calls = calls <= UINT32_MAX ? (unsigned)calls : 0;
        text(cJSON_GetArrayItem(x, 8), value.top_model, sizeof(value.top_model));
        if (ok) *out = value;
    }
    cJSON_Delete(root);
    return ok;
}
void magpie_model_init(magpie_model_t *m, uint64_t now)
{
    memset(m, 0, sizeof(*m)); m->awake = true; m->activity_ms = now;
}
void magpie_connection(magpie_model_t *m, bool connected)
{
    m->connected = connected; m->valid = false; m->data.sequence = 0; m->pairing = false;
}
bool magpie_receive(magpie_model_t *m, const char *json, size_t length, uint64_t now)
{
    if (!magpie_parse(json, length, m->data.sequence, &m->data)) return false;
    m->received_ms = now; m->valid = true; m->pairing = false; return true;
}
magpie_link_t magpie_link(const magpie_model_t *m, uint64_t now)
{
    if (!m->connected) return MAGPIE_OFFLINE;
    if (!m->valid) return MAGPIE_SYNCING;
    return now - m->received_ms >= MAGPIE_STALE_MS ? MAGPIE_STALE : MAGPIE_ONLINE;
}
void magpie_tick(magpie_model_t *m, uint64_t now)
{
    if (!m->pairing && now - m->activity_ms >= MAGPIE_IDLE_MS) {
        m->awake = false; m->actions = false; m->clear_confirm = false;
    }
}
magpie_effect_t magpie_key(magpie_model_t *m, magpie_key_t key, bool held, uint64_t now)
{
    m->activity_ms = now;
    if (!m->awake) { m->awake = true; return EFFECT_NONE; }
    if (m->pairing) return EFFECT_NONE;
    if (m->clear_confirm) {
        m->clear_confirm = false;
        return key == KEY_OK && held ? EFFECT_CLEAR_BONDS : EFFECT_NONE;
    }
    if (held && key == KEY_OK) { m->actions = !m->actions; m->selection = 0; return EFFECT_NONE; }
    if (held) return EFFECT_NONE;
    if (m->actions) {
        if (key == KEY_UP) m->selection = (m->selection + 3) % 4;
        else if (key == KEY_DOWN) m->selection = (m->selection + 1) % 4;
        else {
            m->actions = false;
            if (m->selection == 2) { m->clear_confirm = true; return EFFECT_NONE; }
            if (m->selection == 3) return EFFECT_NONE;
            return m->selection == 0 ? EFFECT_REFRESH : EFFECT_FOCUS;
        }
    } else if (key == KEY_UP) m->page = (m->page + 3) % 4;
    else if (key == KEY_DOWN) m->page = (m->page + 1) % 4;
    else return EFFECT_REFRESH;
    return EFFECT_NONE;
}
void magpie_compact(uint64_t value, char *out, size_t size)
{
    static const char *suffix[] = {"", "K", "M", "B", "T", "P"};
    double scaled = value; unsigned unit = 0;
    while (scaled >= 1000 && unit < 5) { scaled /= 1000; ++unit; }
    if (unit == 0) snprintf(out, size, "%llu", (unsigned long long)value);
    else snprintf(out, size, "%.1f%s", scaled, suffix[unit]);
}

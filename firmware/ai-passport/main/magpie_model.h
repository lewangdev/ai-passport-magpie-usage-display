#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAGPIE_PAYLOAD_MAX 256
#define MAGPIE_STALE_MS 60000
#define MAGPIE_IDLE_MS 60000

typedef struct {
    uint32_t sequence;
    uint64_t unix_time, tokens_today;
    int utc_offset;
    unsigned remaining, calls, errors, success, ttft;
    char latest_model[32], top_model[32];
    unsigned status, latency;
    bool failed;
    uint64_t sessions, total_calls, input, output, cache, reasoning, cost_micros;
    unsigned cache_hit, top_calls;
} magpie_snapshot_t;

typedef enum { MAGPIE_OFFLINE, MAGPIE_SYNCING, MAGPIE_ONLINE, MAGPIE_STALE } magpie_link_t;
typedef enum { KEY_UP, KEY_DOWN, KEY_OK } magpie_key_t;
typedef enum { EFFECT_NONE, EFFECT_REFRESH, EFFECT_FOCUS, EFFECT_CLEAR_BONDS } magpie_effect_t;
typedef struct {
    magpie_snapshot_t data;
    bool connected, valid, awake, pairing, actions, clear_confirm;
    unsigned page, selection;
    uint64_t received_ms, activity_ms;
} magpie_model_t;

bool magpie_json_frame(const char *json, size_t length);
bool magpie_parse(const char *json, size_t length, uint32_t previous, magpie_snapshot_t *out);
void magpie_model_init(magpie_model_t *model, uint64_t now);
void magpie_connection(magpie_model_t *model, bool connected);
bool magpie_receive(magpie_model_t *model, const char *json, size_t length, uint64_t now);
magpie_link_t magpie_link(const magpie_model_t *model, uint64_t now);
void magpie_tick(magpie_model_t *model, uint64_t now);
magpie_effect_t magpie_key(magpie_model_t *model, magpie_key_t key, bool long_press, uint64_t now);
void magpie_compact(uint64_t value, char *out, size_t size);

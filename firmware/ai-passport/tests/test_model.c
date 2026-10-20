#include "magpie_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char payload[] = "{\"v\":4,\"s\":1,\"t\":1792500000,\"o\":480,\"r\":60,\"d\":100,\"f\":8,\"z\":1,\"y\":875,\"l\":23,\"m\":\"gpt-test\",\"h\":200,\"n\":123,\"b\":0,\"x\":[3,9,100,20,30,4,56000,600,\"gpt-test\",7]}";
int main(int argc, char **argv)
{
    magpie_snapshot_t data = {0};
    if (argc > 1) {
        char input[1024]; size_t n = fread(input, 1, sizeof(input), stdin);
        if (!magpie_parse(input, n, 0, &data)) return 2;
        printf("%lu %llu %u %llu %s\n", (unsigned long)data.sequence,
            (unsigned long long)data.tokens_today, data.remaining,
            (unsigned long long)data.cost_micros, data.top_model);
        return 0;
    }
    (void)argv;
    assert(!magpie_json_frame("[[[[[0]]]]]", 11));
    assert(magpie_json_frame("{\"m\":\"[\\\"{\"}", 12));
    assert(magpie_parse(payload, strlen(payload), 0, &data));
    assert(data.success == 875 && data.input == 100 && data.cache == 30 && data.reasoning == 4);
    assert(data.utc_offset == 480 && data.cost_micros == 56000 && !strcmp(data.latest_model, "gpt-test"));
    assert(magpie_parse(payload, strlen(payload), 1, &data)); // same snapshot heartbeat
    assert(!magpie_parse(payload, strlen(payload), 2, &data));
    assert(!magpie_parse(payload, strlen(payload)-1, 0, &data));
    assert(!magpie_parse("{}", 2, 0, &data));
    assert(!magpie_parse(payload, 257, 0, &data));
    char broken[sizeof(payload)+10]; snprintf(broken, sizeof(broken), "%sX", payload);
    assert(!magpie_parse(broken, strlen(broken), 0, &data));
    memcpy(broken, payload, sizeof(payload)); broken[5] = '3';
    assert(!magpie_parse(broken, strlen(broken), 0, &data));
    memcpy(broken, payload, sizeof(payload)); broken[20] = 0;
    assert(!magpie_parse(broken, strlen(payload), 0, &data));
    magpie_model_t m; magpie_model_init(&m, 0);
    assert(magpie_link(&m, 0) == MAGPIE_OFFLINE);
    magpie_connection(&m, true); assert(magpie_link(&m, 0) == MAGPIE_SYNCING);
    assert(magpie_receive(&m, payload, strlen(payload), 100));
    assert(magpie_link(&m, 60099) == MAGPIE_ONLINE);
    assert(magpie_link(&m, 60100) == MAGPIE_STALE);
    assert(magpie_key(&m, KEY_UP, false, 110) == EFFECT_NONE && m.page == 3);
    magpie_key(&m, KEY_DOWN, false, 120); assert(m.page == 0);
    assert(magpie_key(&m, KEY_OK, false, 130) == EFFECT_REFRESH);
    magpie_key(&m, KEY_OK, true, 140); assert(m.actions);
    magpie_key(&m, KEY_DOWN, false, 150);
    assert(magpie_key(&m, KEY_OK, false, 160) == EFFECT_FOCUS && !m.actions);
    magpie_key(&m, KEY_OK, true, 170); magpie_key(&m, KEY_DOWN, false, 180); magpie_key(&m, KEY_DOWN, false, 190);
    assert(magpie_key(&m, KEY_OK, false, 200) == EFFECT_NONE && m.clear_confirm);
    assert(magpie_key(&m, KEY_OK, false, 210) == EFFECT_NONE && !m.clear_confirm);
    m.clear_confirm = true;
    assert(magpie_key(&m, KEY_OK, true, 220) == EFFECT_CLEAR_BONDS);
    magpie_tick(&m, 60220); assert(!m.awake);
    assert(magpie_key(&m, KEY_OK, false, 60230) == EFFECT_NONE && m.awake);
    m.pairing = true; magpie_tick(&m, 999999); assert(m.awake);
    assert(magpie_key(&m, KEY_DOWN, false, 1000000) == EFFECT_NONE && m.page == 0);
    magpie_connection(&m, false); assert(!m.pairing && !m.valid);
    magpie_connection(&m, true); assert(magpie_receive(&m, payload, strlen(payload), 1000100));
    char compact[32]; magpie_compact(1234567, compact, sizeof(compact)); assert(!strcmp(compact, "1.2M"));
    puts("Model/protocol tests: PASS");
    return 0;
}

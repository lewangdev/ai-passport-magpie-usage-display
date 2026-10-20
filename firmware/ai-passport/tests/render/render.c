#include "magpie_ui.h"
#include "bsp_display_rounding.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t framebuffer[240 * 320];
static uint16_t buffer[240 * 40];
static void flush(lv_display_t *display, const lv_area_t *a, uint8_t *pixels)
{
    uint16_t *src = (uint16_t *)pixels;
    for (int y = a->y1; y <= a->y2; ++y) {
        int32_t left, right;
        bsp_display_rounded_row_span(y, 240, 320, 30, &left, &right);
        for (int x = a->x1; x <= a->x2; ++x) framebuffer[y*240+x] = x < left || x > right ? 0 : *src, ++src;
    }
    lv_display_flush_ready(display);
}
static void save(const char *dir, const char *name)
{
    lv_obj_invalidate(lv_screen_active()); lv_refr_now(NULL);
    char path[512]; snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb"); assert(f);
    fprintf(f, "P6\n240 320\n255\n");
    for (unsigned i=0; i<240*320; ++i) {
        uint16_t p = framebuffer[i];
        unsigned char rgb[3] = {(p >> 11)*255/31, ((p >> 5)&63)*255/63, (p&31)*255/31};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    lv_mem_monitor_t memory; lv_mem_monitor(&memory);
    printf("%s: LVGL max used=%u free=%u\n", name, (unsigned)memory.max_used, (unsigned)memory.free_size);
}
int main(int argc, char **argv)
{
    assert(argc == 2); lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    magpie_model_t m; magpie_model_init(&m, 1000);
    magpie_ui_create(); magpie_ui_update(&m, 1000, -1, 0, ""); save(argv[1], "offline");
    m.connected=true; m.valid=true; m.received_ms=1000;
    m.data = (magpie_snapshot_t){.unix_time=1792500000, .utc_offset=480, .remaining=76, .tokens_today=123456,
        .calls=1200, .errors=3, .success=997, .ttft=150, .status=200, .latency=1234,
        .sessions=40, .input=1234567, .output=12345, .cache=987654, .reasoning=3456,
        .cost_micros=6110000, .cache_hit=889, .top_calls=450};
    snprintf(m.data.latest_model, sizeof(m.data.latest_model), "remote-model-with-a-long-name");
    snprintf(m.data.top_model, sizeof(m.data.top_model), "gpt-5.6-sol");
    const char *names[] = {"overview", "requests", "sessions", "context"};
    for (m.page=0; m.page<4; ++m.page) { magpie_ui_update(&m, 1000, 87, 0, ""); save(argv[1], names[m.page]); }
    m.page=0; m.pairing=true; magpie_ui_update(&m, 1000, 87, 123456, ""); save(argv[1], "pairing");
    m.pairing=false; m.actions=true; magpie_ui_update(&m, 1000, 87, 0, ""); save(argv[1], "actions");
    m.actions=false; m.clear_confirm=true; magpie_ui_update(&m, 1000, 87, 0, ""); save(argv[1], "clear-pairing");
    return 0;
}

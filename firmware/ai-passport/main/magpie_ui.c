#include "magpie_ui.h"
#include "lvgl.h"
#include <stdio.h>
#include <time.h>

LV_IMAGE_DECLARE(magpie_logo);
static lv_obj_t *title, *link_label, *battery_label, *hero, *caption, *rows[4], *footer, *clock_label;
static lv_obj_t *overlay, *overlay_title, *overlay_body;
static const uint32_t INK = 0xEEEDE5, MUTED = 0xA9B5BA, ACCENT = 0xAAE477;
static const char *pages[] = {"OVERVIEW", "REQUESTS", "SESSIONS", "CONTEXT"};
static lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y); lv_obj_set_width(obj, width);
    lv_obj_set_height(obj, lv_font_get_line_height(font));
    lv_obj_set_style_text_font(obj, font, 0); lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT); return obj;
}
void magpie_ui_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x111A20), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(INK), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    // Keep all text inside the physical panel's rounded-corner safe area.
    lv_obj_t *logo = lv_image_create(screen); lv_image_set_src(logo, &magpie_logo);
    lv_image_set_scale(logo, 154); lv_obj_set_pos(logo, 12, 4);
    title = label(screen, 49, 15, 116, &lv_font_montserrat_20, INK); lv_label_set_text(title, "magpie");
    battery_label = label(screen, 174, 19, 48, &lv_font_montserrat_14, MUTED);
    lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_RIGHT, 0);
    link_label = label(screen, 20, 43, 200, &lv_font_montserrat_14, ACCENT);
    caption = label(screen, 20, 72, 200, &lv_font_montserrat_14, MUTED);
    hero = label(screen, 20, 94, 200, &lv_font_montserrat_36, ACCENT);
    for (int i = 0; i < 4; ++i) rows[i] = label(screen, 20, 151 + i*27, 200, &lv_font_montserrat_14, INK);
    clock_label = label(screen, 20, 266, 200, &lv_font_montserrat_14, MUTED);
    footer = label(screen, 25, 289, 190, &lv_font_montserrat_14, MUTED);
    lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
    overlay = lv_obj_create(screen); lv_obj_set_pos(overlay, 12, 63); lv_obj_set_size(overlay, 216, 210);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x23323A), 0);
    lv_obj_set_style_border_color(overlay, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(overlay, 1, 0); lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    overlay_title = label(overlay, 12, 14, 190, &lv_font_montserrat_20, ACCENT);
    overlay_body = label(overlay, 12, 49, 190, &lv_font_montserrat_14, INK);
    lv_obj_set_height(overlay_body, 150);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(screen);
}
static void compact_row(lv_obj_t *row, const char *name, uint64_t value)
{
    char compact[24]; magpie_compact(value, compact, sizeof(compact));
    lv_label_set_text_fmt(row, "%s   %s", name, compact);
}
void magpie_ui_update(const magpie_model_t *m, uint64_t now, int battery, uint32_t passkey, const char *toast)
{
    static const char *states[] = {"OFFLINE", "SYNCING", "ONLINE", "STALE"};
    const magpie_snapshot_t *d = &m->data;
    lv_label_set_text_fmt(link_label, "%s  /  %s", states[magpie_link(m, now)], pages[m->page]);
    if (battery < 0) lv_label_set_text(battery_label, "--%");
    else lv_label_set_text_fmt(battery_label, "%d%%", battery);
    for (int i = 0; i < 4; ++i) lv_label_set_text(rows[i], "");
    if (!m->valid) {
        lv_label_set_text(caption, "LOCAL GATEWAY MONITOR"); lv_label_set_text(hero, "--");
        lv_label_set_text(rows[0], "Waiting for Companion"); lv_label_set_text(rows[1], "Run companion/run.sh");
        lv_label_set_text(rows[2], "Pair using the display code");
    } else switch (m->page) {
    case 0:
        lv_label_set_text(caption, "TODAY / SUCCESS RATE");
        lv_label_set_text_fmt(hero, "%u.%u%%", d->success / 10, d->success % 10);
        compact_row(rows[0], "Calls", d->calls); compact_row(rows[1], "Errors", d->errors);
        lv_label_set_text_fmt(rows[2], "Quota remaining   %u%%", d->remaining);
        compact_row(rows[3], "Tokens today", d->tokens_today); break;
    case 1:
        lv_label_set_text(caption, "LATEST REQUEST / HTTP");
        lv_label_set_text_fmt(hero, "%u", d->status);
        lv_label_set_text_fmt(rows[0], "%s", d->latest_model);
        lv_label_set_text_fmt(rows[1], "%s   %u ms", d->failed ? "FAILED" : "OK", d->latency);
        lv_label_set_text_fmt(rows[2], "Average TTFT   %u ms", d->ttft);
        compact_row(rows[3], "Tokens today", d->tokens_today); break;
    case 2: {
        char compact[24]; magpie_compact(d->sessions, compact, sizeof(compact));
        lv_label_set_text(caption, "30 DAYS / SESSIONS"); lv_label_set_text(hero, compact);
        compact_row(rows[0], "Total tokens", d->input + d->output + d->cache + d->reasoning);
        char cost[64]; snprintf(cost, sizeof(cost), "Est. cost   $%.2f", (double)d->cost_micros / 1000000.0);
        lv_label_set_text(rows[1], cost);
        lv_label_set_text_fmt(rows[2], "Top: %s", d->top_model);
        lv_label_set_text_fmt(rows[3], "Top model calls   %u", d->top_calls); break;
    }
    case 3:
        lv_label_set_text(caption, "CONTEXT / CACHE HIT");
        lv_label_set_text_fmt(hero, "%u.%u%%", d->cache_hit / 10, d->cache_hit % 10);
        compact_row(rows[0], "Input", d->input); compact_row(rows[1], "Output", d->output);
        compact_row(rows[2], "Cache read", d->cache); compact_row(rows[3], "Reasoning", d->reasoning); break;
    }
    if (m->valid && d->unix_time) {
        time_t local = (time_t)((int64_t)d->unix_time + (int64_t)d->utc_offset * 60 + (now - m->received_ms) / 1000);
        struct tm value; gmtime_r(&local, &value); char text[32]; strftime(text, sizeof(text), "%m/%d %H:%M", &value);
        lv_label_set_text_fmt(clock_label, "%s     %u/4", text, m->page + 1);
    } else lv_label_set_text_fmt(clock_label, "USB + BLE              %u/4", m->page + 1);
    lv_label_set_text(footer, toast && *toast ? toast : "UP / DOWN    OK: refresh");
    bool show = m->pairing || m->actions || m->clear_confirm;
    if (!show) { lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(clock_label, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_add_flag(clock_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    if (m->pairing) {
        lv_label_set_text(footer, "WAITING FOR MAC");
        lv_label_set_text(overlay_title, "PAIR WITH MAC");
        lv_label_set_text_fmt(overlay_body, "Enter this code on Mac:\n\n       %06lu\n\nKeep this screen visible\nuntil pairing completes.", (unsigned long)passkey);
    } else if (m->clear_confirm) {
        lv_label_set_text(footer, "HOLD OK TO CONFIRM");
        lv_label_set_text(overlay_title, "CLEAR PAIRING?");
        lv_label_set_text(overlay_body, "Hold OK to clear all\nBluetooth bonds and restart.\n\nAny other key cancels.\n\nMac will need to pair again.");
    } else {
        lv_label_set_text(footer, "UP/DOWN   OK: select");
        static const char *options[] = {"Refresh now", "Focus Magpie", "Clear pairing", "Back"};
        lv_label_set_text(overlay_title, "QUICK ACTIONS");
        lv_label_set_text_fmt(overlay_body, "%s %s\n\n%s %s\n\n%s %s\n\n%s %s",
            m->selection == 0 ? ">" : " ", options[0], m->selection == 1 ? ">" : " ", options[1],
            m->selection == 2 ? ">" : " ", options[2], m->selection == 3 ? ">" : " ", options[3]);
    }
}

/**
 * @file lvgl_fixture.c
 * @brief 无头 LVGL 夹具的实现。
 */

#include "lvgl_fixture.h"

#define FIXTURE_HOR_RES 200
#define FIXTURE_VER_RES 100
#define FIXTURE_DRAW_LINES 10

static lv_color_t s_draw_buf[FIXTURE_HOR_RES * FIXTURE_DRAW_LINES];
static uint32_t s_tick_ms;

static uint32_t fixture_tick_get(void) {
    return s_tick_ms;
}

static void fixture_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    (void)area;
    (void)px_map;
    lv_display_flush_ready(disp);
}

void lvgl_fixture_init(void) {
    s_tick_ms = 0;
    lv_tick_set_cb(fixture_tick_get);
    lv_init();
    lv_display_t *disp = lv_display_create(FIXTURE_HOR_RES, FIXTURE_VER_RES);
    lv_display_set_flush_cb(disp, fixture_flush);
    lv_display_set_buffers(
        disp, s_draw_buf, NULL, sizeof(s_draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void lvgl_fixture_step(void) {
    (void)lv_timer_handler();
}

void lvgl_fixture_advance(unsigned ms) {
    s_tick_ms += ms;
    (void)lv_timer_handler();
}

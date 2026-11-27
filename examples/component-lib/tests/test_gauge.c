/**
 * @file test_widget.c
 * @brief 控件(widget class)的单测(design.md §7.3 / §7.4)。
 *
 * 验四件事:类真的造出新类型、props 走 LVGL property、自定义事件真的发、
 * 实例状态与 property 读回一致。
 */
#include <stdio.h>
#include <string.h>

#include "demo/gauge.h"
#include "lvgl_fixture.h"

static int g_fail;

static void check(int ok, const char *what) {
    if (!ok) {
        printf("  FAIL %s\n", what);
        g_fail += 1;
    }
}

static int g_events;

static void on_changed(lv_event_t *e) {
    (void)e;
    g_events += 1;
}

static void test_class_creates_new_type(void) {
    printf("test_class_creates_new_type\n");
    lv_obj_t *g = demo_gauge_create(lv_screen_active());
    check(NULL != g, "create returns a widget");
    check(lv_obj_check_type(g, &demo_gauge_class), "it is a demo_gauge");
    check(!lv_obj_check_type(g, &lv_obj_class), "it is not a bare lv_obj");
    check(&demo_gauge_class == lv_obj_get_class(g), "class pointer round-trips");
}

static void test_props(void) {
    printf("test_props\n");
    lv_obj_t *g = demo_gauge_create(lv_screen_active());
    check(0 == demo_gauge_get_value(g), "starts at 0");

    /* 经 LVGL property 写 —— 与原生 widget 的 props 同一套(§7.4)。 */
    lv_property_t prop = {
        .id = LV_PROPERTY_GAUGE_VALUE,
        .num = 42,
    };
    check(LV_RESULT_OK == lv_obj_set_property(g, &prop), "set property VALUE");
    check(42 == demo_gauge_get_value(g), "property landed in instance state");
    check(42 == lv_obj_get_property(g, LV_PROPERTY_GAUGE_VALUE).num, "property reads back");

    /* 越界收口在 setter 里。 */
    prop.num = 999;
    (void)lv_obj_set_property(g, &prop);
    check(100 == demo_gauge_get_value(g), "clamped to max");
}

static void test_custom_event(void) {
    printf("test_custom_event\n");
    lv_obj_t *g = demo_gauge_create(lv_screen_active());
    lv_obj_add_event_cb(g, on_changed, demo_gauge_event_value_changed, NULL);
    g_events = 0;

    demo_gauge_set_value(g, 50);
    check(1 == g_events, "VALUE_CHANGED fired once");
    demo_gauge_set_value(g, 50);
    check(1 == g_events, "same value does not re-fire");
    demo_gauge_set_value(g, 60);
    check(2 == g_events, "fired again on change");
}

static lv_obj_t *demo_gauge_fill(lv_obj_t *g) {
    return ((demo_gauge_t *)g)->fill;
}

static int32_t demo_gauge_max(lv_obj_t *g) {
    return ((demo_gauge_t *)g)->max;
}

/* --- 5. 绑定胶水:第三方控件也能接上响应式(§6)----------------------- */

static void test_binding(void) {
    printf("test_binding\n");
    lv_obj_t *g = demo_gauge_create(lv_screen_active());
    int32_t v = 30;
    shrewd_basic_reflect_ref_t ref;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ref, &v, sizeof(v)), "ref_init");
    check(SHREWD_LV_VIEW_ERR_OK == demo_bind_gauge_value(g, &ref), "bind_gauge_value");

    check(30 == demo_gauge_get_value(g), "ref drives the widget on bind");
    v = 70;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_set(&ref, &v, sizeof(v)), "ref_set");
    check(70 == demo_gauge_get_value(g), "widget follows the ref");

    demo_gauge_set_value(g, 20);
    check(20 == v, "widget writes back through the ref");
}

static void test_instance_size(void) {
    printf("test_instance_size\n");
    /* instance_size 是 16 位位域:太大直接放不下,这是控件作者要知道的约束。 */
    check(sizeof(demo_gauge_t) < 65536, "instance fits the 16-bit instance_size field");
    lv_obj_t *g = demo_gauge_create(lv_screen_active());
    check(NULL != demo_gauge_fill(g), "constructor built the inner bar");
    check(0 != demo_gauge_max(g), "constructor set a max");
}

int main(void) {
    lvgl_fixture_init();
    check(SHREWD_LV_VIEW_ERR_OK == demo_gauge_init(), "init registers the event id");
    check(0 != demo_gauge_event_value_changed, "event id is non-zero");
    test_class_creates_new_type();
    test_props();
    test_custom_event();
    test_binding();
    test_instance_size();
    if (0 == g_fail) {
        printf("test_widget: ok\n");
        return 0;
    }
    printf("test_widget: FAILED (%d)\n", g_fail);
    return 1;
}

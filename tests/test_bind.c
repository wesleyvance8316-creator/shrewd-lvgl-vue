/**
 * @file test_bind.c
 * @brief 绑定机制的单测(design.md §2 / §3 / §4)。
 *
 * 断言一律查状态:LVGL 属性读回、ref 槽位、以及自定义 target 的调用计数。
 */
#include <stdio.h>
#include <string.h>

#include "lvgl_fixture.h"
#include "shrewd-lv-view/view.h"

static int g_fail;

static void check(int ok, const char *what) {
    if (!ok) {
        printf("  FAIL %s\n", what);
        g_fail += 1;
    }
}

/* --- 一个会主动发事件的 target,用来观察 applying 抑制 ---------------- */

static int g_read_calls;

static void poke_write(void *obj, const void *value, size_t size) {
    (void)value;
    (void)size;
    /* 模拟 widget 自己触发写回事件。 */
    (void)lv_obj_send_event((lv_obj_t *)obj, LV_EVENT_VALUE_CHANGED, NULL);
}

static int poke_read(void *obj, void *out, size_t size) {
    (void)obj;
    g_read_calls += 1;
    if (sizeof(int32_t) == size) {
        *(int32_t *)out = 0;
    }
    return SHREWD_LV_VIEW_ERR_OK;
}

static const shrewd_lv_view_target_t g_poke = {
    .write = poke_write,
    .read = poke_read,
    .changed_event = LV_EVENT_VALUE_CHANGED,
};

/* --- 1. 单向:ref → widget ------------------------------------------ */

static void test_one_way(void) {
    printf("test_one_way\n");
    lv_obj_t *label = lv_label_create(lv_screen_active());

    char buf[32];
    memset(buf, 0, sizeof(buf));
    shrewd_basic_reflect_ref_t ref;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ref, buf, sizeof(buf)), "ref_init");
    (void)strncpy(buf, "hello", sizeof(buf) - 1);

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bind_label_text(label, &ref), "bind_label_text");
    check(0 == strcmp("hello", lv_label_get_text(label)), "label shows initial ref");

    (void)strncpy(buf, "world", sizeof(buf) - 1);
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_set(&ref, buf, sizeof(buf)), "ref_set");
    check(0 == strcmp("world", lv_label_get_text(label)), "ref change propagates to label");
}

/* --- 2. 双向:widget → ref ------------------------------------------ */

static void test_two_way(void) {
    printf("test_two_way\n");
    lv_obj_t *slider = lv_slider_create(lv_screen_active());
    lv_slider_set_range(slider, 0, 100);

    int32_t value = 0;
    shrewd_basic_reflect_ref_t ref;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ref, &value, sizeof(value)),
          "ref_init");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bind_slider_value(slider, &ref),
          "bind_slider_value");

    /* ref → widget */
    value = 30;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_set(&ref, &value, sizeof(value)),
          "ref_set");
    check(30 == lv_slider_get_value(slider), "ref change propagates to slider");

    /* widget → ref:模拟用户拖动(直接改值再发事件) */
    lv_slider_set_value(slider, 77, LV_ANIM_OFF);
    (void)lv_obj_send_event(slider, LV_EVENT_VALUE_CHANGED, NULL);
    check(77 == value, "slider change propagates back to ref");
}

/* --- 3. applying 抑制:ref_set 引发的写入不应触发写回 ---------------- */

static void test_applying_guard(void) {
    printf("test_applying_guard\n");
    lv_obj_t *obj = lv_obj_create(lv_screen_active());

    int32_t value = 1;
    shrewd_basic_reflect_ref_t ref;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ref, &value, sizeof(value)),
          "ref_init");

    g_read_calls = 0;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bind(obj, &g_poke, &ref), "bind");
    check(0 == g_read_calls, "effect's own write does not call read");

    /* 再来一次:ref_set → effect → write → 事件 → 应被抑制 */
    value = 2;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_set(&ref, &value, sizeof(value)),
          "ref_set");
    check(0 == g_read_calls, "write-back suppressed while applying");

    /* 真正的 widget 改动应当写回 */
    (void)lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, NULL);
    check(1 == g_read_calls, "external change does write back");
}

/* --- 4. DELETE 自清:widget 删除后不许再碰它 ------------------------- */

static void test_delete_cleanup(void) {
    printf("test_delete_cleanup\n");
    lv_obj_t *label = lv_label_create(lv_screen_active());

    char buf[32];
    memset(buf, 0, sizeof(buf));
    shrewd_basic_reflect_ref_t ref;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ref, buf, sizeof(buf)), "ref_init");

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bind_label_text(label, &ref), "bind_label_text");
    lv_obj_delete(label);

    /* 绑定应已自清:再改 ref 不得触碰已删除的 widget(否则 use-after-free)。 */
    (void)strncpy(buf, "after-delete", sizeof(buf) - 1);
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_set(&ref, buf, sizeof(buf)),
          "ref_set after delete");
}

int main(void) {
    lvgl_fixture_init();
    test_one_way();
    test_two_way();
    test_applying_guard();
    test_delete_cleanup();
    if (0 == g_fail) {
        printf("test_bind: ok\n");
        return 0;
    }
    printf("test_bind: FAILED (%d)\n", g_fail);
    return 1;
}

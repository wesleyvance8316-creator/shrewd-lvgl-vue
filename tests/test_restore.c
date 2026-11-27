/**
 * @file test_restore.c
 * @brief 栈恢复的单测(architecture.md §6.11 Q3 / design.md §8.6)。
 *
 * 模拟真实场景:**进程重启后从快照回到原页面** —— dispose + init +
 * 登记 + restore。这是"GUI 常驻、看门狗复位"的落法。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl_fixture.h"
#include "shrewd-basic/reflect/reflect.h"
#include "shrewd-lv-view/view.h"

static int g_fail;

static void check(int ok, const char *what) {
    if (!ok) {
        printf("  FAIL %s\n", what);
        g_fail += 1;
    }
}

typedef struct {
    shrewd_lv_view_page_t base;
    int create_calls;
    int enter_calls;
    int32_t param;
} test_page_t;

static int test_create(shrewd_lv_view_page_t *p, lv_obj_t *container, const void *params) {
    test_page_t *t = (test_page_t *)p;
    t->create_calls += 1;
    p->root = lv_obj_create(container);
    if (NULL != params) {
        t->param = *(const int32_t *)params;
    }
    return SHREWD_LV_VIEW_ERR_OK;
}

static int test_enter(shrewd_lv_view_page_t *p) {
    ((test_page_t *)p)->enter_calls += 1;
    return SHREWD_LV_VIEW_ERR_OK;
}

static const shrewd_basic_reflect_type_t g_i32 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_INT32,
    .sb_refl_name = "int32",
    .sb_refl_size = sizeof(int32_t),
};

/* 每个页面一个描述符:name 是它的身份,不能共享。 */
#define PAGE_DESC(nm)                                                                              \
    static const shrewd_lv_view_page_desc_t g_desc_##nm = {                                        \
        .name = #nm,                                                                               \
        .param_type = &g_i32,                                                                      \
        .release = SHREWD_LV_VIEW_RELEASE_DESTROY_ON_EXIT,                                         \
        .cache_count = 0,                                                                          \
        .create = test_create,                                                                     \
        .enter = test_enter,                                                                       \
    }

PAGE_DESC(home);
PAGE_DESC(settings);
PAGE_DESC(detail);

static void page_init(test_page_t *p, const shrewd_lv_view_page_desc_t *desc) {
    memset(p, 0, sizeof(*p));
    p->base.desc = desc;
}

/* --- 1. 往返:存 → 重启 → 恢复 --------------------------------------- */

static void test_round_trip(void) {
    printf("test_round_trip\n");
    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK
              == shrewd_lv_view_nav_init(&nav, lv_screen_active(), lv_layer_top(), 0),
          "nav_init");

    static test_page_t a;
    static test_page_t b;
    static test_page_t c;
    page_init(&a, &g_desc_home);
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &a.base, NULL), "push home");
    page_init(&b, &g_desc_settings);
    int32_t pv = 7;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &b.base, &pv), "push settings");
    page_init(&c, &g_desc_detail);
    int32_t cv = 99;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &c.base, &cv), "push detail");
    check(3 == shrewd_lv_view_nav_depth(&nav), "depth 3 before save");

    size_t size = 0;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_save_size(&nav, &size), "save_size");
    void *buf = malloc(size);
    check(NULL != buf, "malloc snapshot");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_save(&nav, buf, size), "save");

    /* 重启:实例复用(真实里是新的静态实例),登记后恢复。 */
    shrewd_lv_view_nav_dispose(&nav);
    check(SHREWD_LV_VIEW_ERR_OK
              == shrewd_lv_view_nav_init(&nav, lv_screen_active(), lv_layer_top(), 0),
          "nav_init (restart)");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_register(&nav, &a.base), "register home");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_register(&nav, &b.base), "register settings");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_register(&nav, &c.base), "register detail");

    /* 重启后重新计数:save 前那轮 create/enter 不属于这次。 */
    a.create_calls = a.enter_calls = 0;
    b.create_calls = b.enter_calls = 0;
    c.create_calls = c.enter_calls = 0;

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_restore(&nav, buf, size), "restore");
    free(buf);

    check(3 == shrewd_lv_view_nav_depth(&nav), "depth 3 after restore");
    check(&c.base == shrewd_lv_view_nav_top(&nav), "top is detail");
    check((1 == c.create_calls) && (1 == c.enter_calls), "top built and entered");
    check(0 == a.create_calls, "a not built (below top)");
    check(0 == b.create_calls, "b not built (below top)");
    check(99 == c.param, "params restored into the stack entry");

    /* 回退到 settings:b 的参数要还在。 */
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_pop(&nav), "pop");
    check(&b.base == shrewd_lv_view_nav_top(&nav), "top is settings");
    check(7 == b.param, "params survive the round trip");
}

/* --- 2. 未知页面名:恢复失败 ------------------------------------------ */

static void test_unknown_page(void) {
    printf("test_unknown_page\n");
    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK
              == shrewd_lv_view_nav_init(&nav, lv_screen_active(), lv_layer_top(), 0),
          "nav_init");

    static test_page_t a;
    page_init(&a, &g_desc_home);
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &a.base, NULL), "push");

    size_t size = 0;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_save_size(&nav, &size), "save_size");
    void *buf = malloc(size);
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_save(&nav, buf, size), "save");

    /* 不登记就恢复:找不到 "home" */
    shrewd_lv_view_nav_dispose(&nav);
    check(SHREWD_LV_VIEW_ERR_OK
              == shrewd_lv_view_nav_init(&nav, lv_screen_active(), lv_layer_top(), 0),
          "nav_init");
    check(SHREWD_LV_VIEW_ERR_NOT_FOUND == shrewd_lv_view_nav_restore(&nav, buf, size),
          "restore fails: page not registered");
    check(0 == shrewd_lv_view_nav_depth(&nav), "stack stays empty");
    free(buf);
}

/* --- 3. 坏快照:恢复失败 --------------------------------------------- */

static void test_corrupt_snapshot(void) {
    printf("test_corrupt_snapshot\n");
    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK
              == shrewd_lv_view_nav_init(&nav, lv_screen_active(), lv_layer_top(), 0),
          "nav_init");

    unsigned char junk[32];
    memset(junk, 0, sizeof(junk));
    check(SHREWD_LV_VIEW_ERR_ARG_INVALID == shrewd_lv_view_nav_restore(&nav, junk, sizeof(junk)),
          "bad magic rejected");

    static test_page_t a;
    page_init(&a, &g_desc_home);
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &a.base, NULL), "push");
    size_t size = 0;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_save_size(&nav, &size), "save_size");
    void *buf = malloc(size);
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_save(&nav, buf, size), "save");
    check(SHREWD_LV_VIEW_ERR_ARG_INVALID == shrewd_lv_view_nav_restore(&nav, buf, size - 1),
          "truncated snapshot rejected");
    free(buf);
}

int main(void) {
    lvgl_fixture_init();
    test_round_trip();
    test_unknown_page();
    test_corrupt_snapshot();
    if (0 == g_fail) {
        printf("test_restore: ok\n");
        return 0;
    }
    printf("test_restore: FAILED (%d)\n", g_fail);
    return 1;
}
